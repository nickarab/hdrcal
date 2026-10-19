// Implements the interactive calibration session and its cleanup guarantees.

#include "hdrcal/wizard_session.hpp"

#include "hdrcal/atomic_file.hpp"
#include "hdrcal/hypr.hpp"
#include "hdrcal/key_source.hpp"
#include "hdrcal/mpv.hpp"
#include "hdrcal/number_format.hpp"
#include "hdrcal/pattern.hpp"
#include "hdrcal/pq.hpp"
#include "hdrcal/preflight.hpp"
#include "hdrcal/process.hpp"
#include "hdrcal/profile.hpp"
#include "hdrcal/watchdog.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <signal.h>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace hdrcal {
namespace {

volatile sig_atomic_t g_terminationSignal = 0;

extern "C" void handleTerminationSignal(int signalNumber) {
    g_terminationSignal = signalNumber;
}

class SignalHandlerGuard {
public:
    SignalHandlerGuard() {
        struct sigaction action{};
        action.sa_handler = handleTerminationSignal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;

        (void)::sigaction(SIGINT, &action, &m_previousInterrupt);
        (void)::sigaction(SIGTERM, &action, &m_previousTerminate);
    }

    ~SignalHandlerGuard() {
        (void)::sigaction(SIGINT, &m_previousInterrupt, nullptr);
        (void)::sigaction(SIGTERM, &m_previousTerminate, nullptr);
    }

    SignalHandlerGuard(const SignalHandlerGuard&) = delete;
    SignalHandlerGuard& operator=(const SignalHandlerGuard&) = delete;

private:
    struct sigaction m_previousInterrupt{};
    struct sigaction m_previousTerminate{};
};

struct SessionDirectory {
    std::filesystem::path path;

    SessionDirectory() = default;
    explicit SessionDirectory(std::filesystem::path directoryPath) : path(std::move(directoryPath)) {}

    SessionDirectory(const SessionDirectory&) = delete;
    SessionDirectory& operator=(const SessionDirectory&) = delete;

    SessionDirectory(SessionDirectory&& other) noexcept : path(std::move(other.path)) {
        other.path.clear();
    }

    SessionDirectory& operator=(SessionDirectory&& other) noexcept {
        if (this == &other)
            return *this;

        removeDirectory();
        path = std::move(other.path);
        other.path.clear();
        return *this;
    }

    ~SessionDirectory() {
        removeDirectory();
    }

private:
    void removeDirectory() {
        if (path.empty())
            return;
        std::error_code filesystemError;
        std::filesystem::remove_all(path, filesystemError);
        path.clear();
    }
};

struct SessionDirectoryResult {
    std::optional<SessionDirectory> directory;
    std::optional<std::string> error;
};

SessionDirectoryResult createSessionDirectory() {
    std::error_code filesystemError;
    const std::filesystem::path baseDirectory = std::filesystem::temp_directory_path(filesystemError);
    if (filesystemError)
        return {std::nullopt, "não foi possível localizar o diretório temporário: " + filesystemError.message()};

    const std::string processId = std::to_string(static_cast<long long>(::getpid()));

    for (int attempt = 0; attempt < 128; ++attempt) {
        const std::filesystem::path candidate = baseDirectory / ("hdrcal-" + processId + "-" + std::to_string(attempt));
        if (!std::filesystem::create_directory(candidate, filesystemError)) {
            if (filesystemError)
                return {std::nullopt, "não foi possível criar o diretório da sessão: " + filesystemError.message()};
            continue;
        }

        std::filesystem::permissions(candidate, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, filesystemError);
        if (filesystemError)
            return {std::nullopt, "não foi possível ajustar as permissões do diretório da sessão: " + filesystemError.message()};

        return {SessionDirectory(candidate), std::nullopt};
    }

    return {std::nullopt, "não foi possível criar um diretório temporário exclusivo"};
}

std::string formatPreflightFailure(const PreflightResult& result) {
    std::string message = "as pré-condições de HDR falharam:\n";
    for (const PreflightCheck& check : result.checks) {
        if (!check.passed) {
            message += "[FAIL] ";
            message += check.name;
            message += " — ";
            message += check.detail;
            message += "\n";
        }
    }
    return message;
}

std::optional<std::string> findMonitorResolution(const HyprController& controller, const std::string& outputName, int& width, int& height) {
    const HyprMonitorListResult monitors = controller.listMonitors();
    if (!monitors.value)
        return monitors.error.value_or("lista de monitores indisponível");

    for (const HyprMonitorSummary& monitor : *monitors.value) {
        if (monitor.name != outputName)
            continue;
        if (monitor.width <= 0 || monitor.height <= 0)
            return "não foi possível determinar a resolução de " + outputName;
        width = monitor.width;
        height = monitor.height;
        return std::nullopt;
    }

    return "monitor '" + outputName + "' não encontrado em hyprctl monitors";
}

std::string phaseInstruction(PatternPhase phase) {
    switch (phase) {
    case PatternPhase::MinimumLuminance:
        return "Fase 1/3 — luminância mínima: ajuste a mancha escura até ela ficar apenas visível. "
               "Setas = 1 código | Shift+Setas = 10 | Enter = salvar | Esc = abortar";
    case PatternPhase::MaximumLuminance:
        return "Fase 2/3 — luminância máxima: aumente até o xadrez da mancha desaparecer e volte um passo. "
               "Setas = 1 código | Shift+Setas = 10 | Enter = salvar | Esc = abortar";
    case PatternPhase::MaximumFullFrameLuminance:
        return "Fase 3/3 — frame completo: aumente até o brilho máximo estável da tela cheia. "
               "Setas = 1 código | Shift+Setas = 10 | Enter = salvar | Esc = abortar";
    }
    return {};
}

std::string describeCurrentCode(const CalibrationWizard& wizard) {
    const double luminanceNits = computeLuminanceFromPqCode(computePqCodeFromTenBitSample(wizard.currentTenBitCode()));
    return "Código " + std::to_string(wizard.currentTenBitCode()) + "/1023 — " + formatLocaleIndependentDouble(luminanceNits) + " nits";
}

std::optional<std::string> showPhaseInstruction(MpvIpcClient& ipc, const CalibrationWizard& wizard) {
    return ipc.showPermanentInstruction(phaseInstruction(wizard.currentPhase()));
}

std::optional<std::string> showCurrentCode(MpvIpcClient& ipc, const CalibrationWizard& wizard) {
    return ipc.showText(describeCurrentCode(wizard), 1200);
}

constexpr std::string_view kMpvKeyForwardScript = R"lua(
local fifo = os.getenv("HDRCAL_KEY_FIFO")
local file = nil

local function send(key)
    if not fifo then
        return
    end
    if not file then
        file = io.open(fifo, "w")
    end
    if not file then
        mp.msg.error("hdrcal: cannot open key FIFO")
        return
    end
    file:write(key, "\n")
    file:flush()
end

local function bind(key, name, value, repeatable)
    local ok, err = pcall(mp.add_forced_key_binding, key, name, function() send(value) end, { repeatable = repeatable })
    if not ok then
        mp.msg.warn("hdrcal: cannot bind " .. key .. ": " .. tostring(err))
    end
end

-- Arrows repeat while held (mpv autorepeat). Enter/Escape stay single-shot:
-- holding them must not save several phases or abort in a loop.
bind("LEFT", "hdrcal-left", "left", true)
bind("RIGHT", "hdrcal-right", "right", true)
bind("Shift+LEFT", "hdrcal-shift-left", "shift-left", true)
bind("Shift+RIGHT", "hdrcal-shift-right", "shift-right", true)
bind("ENTER", "hdrcal-enter", "enter", false)
bind("ESC", "hdrcal-escape", "escape", false)
)lua";

std::vector<std::string> buildMpvArguments(const std::string& outputName, const std::filesystem::path& socketPath, const std::filesystem::path& scriptPath,
                                           const std::filesystem::path& logPath, const std::filesystem::path& patternPath) {
    return {
        "mpv",
        "--no-config",
        "--really-quiet",
        "--fullscreen",
        "--fs-screen-name=" + outputName,
        "--no-border",
        "--no-osc",
        "--no-input-default-bindings",
        "--image-display-duration=inf",
        "--keep-open=yes",
        "--target-colorspace-hint=yes",
        "--vo=gpu-next",
        "--tone-mapping=clip",
        "--hdr-compute-peak=no",
        "--scale=nearest",
        "--cursor-autohide=always",
        // Deterministic autorepeat: 200 ms before the first repeat, then 10/s.
        // Each repeat regenerates a 2560x1440 16-bit PNG (~54 ms measured) and
        // reloads it through IPC; mpv's default 40/s would overflow the FIFO
        // and keep moving after the key is released. The state machine still
        // applies the existing coarse step after one second of continuous
        // hold.
        "--native-keyrepeat=no",
        "--input-ar-delay=200",
        "--input-ar-rate=10",
        "--input-ipc-server=" + socketPath.string(),
        "--script=" + scriptPath.string(),
        "--log-file=" + logPath.string(),
        patternPath.string(),
    };
}

} // namespace

WizardOutcome runCalibrationWizard(const WizardOptions& options) {
    WizardOutcome outcome;

    g_terminationSignal = 0;
    SignalHandlerGuard signalHandlerGuard;

    std::cout << "Verificando as pré-condições de HDR em " << options.outputName << "...\n";
    std::cout.flush();

    const HyprController controller = HyprController::createWithSystemExecutor();
    const PreflightResult preflight = checkHdrPreconditions(controller, options.outputName, readKmsConnectorState, options.hyprlandConfigDirectory);
    if (!preflight.allPassed()) {
        outcome.error = formatPreflightFailure(preflight);
        return outcome;
    }

    int patternWidth = 0;
    int patternHeight = 0;
    if (const auto resolutionError = findMonitorResolution(controller, options.outputName, patternWidth, patternHeight); resolutionError.has_value()) {
        outcome.error = resolutionError;
        return outcome;
    }

    if (g_terminationSignal != 0) {
        outcome.aborted = true;
        outcome.terminationSignal = g_terminationSignal;
        return outcome;
    }

    SessionDirectoryResult directoryResult = createSessionDirectory();
    if (!directoryResult.directory) {
        outcome.error = directoryResult.error.value_or("não foi possível criar o diretório da sessão");
        return outcome;
    }
    SessionDirectory sessionDirectory = std::move(*directoryResult.directory);

    SessionRestoreWatchdogStartResult watchdogResult = SessionRestoreWatchdog::start({"hyprctl", "reload"});
    if (!watchdogResult.watchdog) {
        outcome.error = watchdogResult.error.value_or("não foi possível iniciar o watchdog de restauração");
        return outcome;
    }
    std::optional<SessionRestoreWatchdog> watchdog = std::move(watchdogResult.watchdog);

    TemporaryLuminanceRule luminanceRuleGuard(controller, readKmsConnectorState, options.hyprlandConfigDirectory);
    if (const auto applyError = luminanceRuleGuard.apply(options.outputName); applyError.has_value()) {
        watchdog->stopGracefully();
        outcome.error = applyError;
        return outcome;
    }

    auto restoreSession = [&]() -> std::optional<std::string> {
        const std::optional<std::string> restoreError = luminanceRuleGuard.restore();
        if (restoreError.has_value())
            watchdog->abandon();
        else
            watchdog->stopGracefully();
        return restoreError;
    };

    const std::filesystem::path fifoPath = sessionDirectory.path / "keys.fifo";
    const std::filesystem::path scriptPath = sessionDirectory.path / "key-forward.lua";
    const std::filesystem::path patternPath = sessionDirectory.path / "pattern.png";
    const std::filesystem::path socketPath = sessionDirectory.path / "mpv.sock";
    const std::filesystem::path logPath = sessionDirectory.path / "mpv.log";

    FifoKeySourceCreateResult keySourceResult = FifoKeySource::create(fifoPath);
    if (!keySourceResult.source) {
        (void)restoreSession();
        outcome.error = keySourceResult.error.value_or("não foi possível criar o FIFO de teclas");
        return outcome;
    }
    FifoKeySource keySource = std::move(*keySourceResult.source);

    if (::setenv("HDRCAL_KEY_FIFO", fifoPath.c_str(), 1) != 0) {
        (void)restoreSession();
        outcome.error = "não foi possível configurar o FIFO de teclas no ambiente";
        return outcome;
    }

    if (const auto scriptError = writeFileAtomically(scriptPath, kMpvKeyForwardScript); scriptError.has_value()) {
        (void)::unsetenv("HDRCAL_KEY_FIFO");
        (void)restoreSession();
        outcome.error = scriptError;
        return outcome;
    }

    CalibrationWizard wizard;
    auto writeCurrentPattern = [&]() -> std::optional<std::string> {
        return writePqPatternPng(
            PatternSpec{.phase = wizard.currentPhase(), .tenBitCode = wizard.currentTenBitCode(), .width = patternWidth, .height = patternHeight}, patternPath);
    };

    if (const auto patternError = writeCurrentPattern(); patternError.has_value()) {
        (void)::unsetenv("HDRCAL_KEY_FIFO");
        (void)restoreSession();
        outcome.error = patternError;
        return outcome;
    }

    std::cout << "Pré-condições OK. Abrindo mpv em fullscreen (" << patternWidth << "x" << patternHeight << ")...\n";
    std::cout << phaseInstruction(wizard.currentPhase()) << "\n";
    std::cout.flush();

    SpawnedProcessResult spawnedMpv = spawnProcess(buildMpvArguments(options.outputName, socketPath, scriptPath, logPath, patternPath));
    if (!spawnedMpv.process) {
        (void)::unsetenv("HDRCAL_KEY_FIFO");
        (void)restoreSession();
        outcome.error = spawnedMpv.error.value_or("não foi possível iniciar o mpv");
        return outcome;
    }
    ChildProcess mpv = std::move(*spawnedMpv.process);
    (void)::unsetenv("HDRCAL_KEY_FIFO");

    if (const auto watchdogError = watchdog->reportMpvProcess(mpv.pid()); watchdogError.has_value()) {
        mpv.terminate();
        (void)restoreSession();
        outcome.error = watchdogError;
        return outcome;
    }

    MpvIpcClient ipc;
    if (const auto connectError = ipc.connectToSocket(socketPath, std::chrono::milliseconds(5000)); connectError.has_value()) {
        mpv.terminate();
        (void)restoreSession();
        outcome.error = connectError;
        return outcome;
    }

    if (const auto instructionError = showPhaseInstruction(ipc, wizard); instructionError.has_value()) {
        (void)ipc.quit();
        mpv.terminate();
        (void)restoreSession();
        outcome.error = instructionError;
        return outcome;
    }

    if (const auto osdError = showCurrentCode(ipc, wizard); osdError.has_value()) {
        (void)ipc.quit();
        mpv.terminate();
        (void)restoreSession();
        outcome.error = osdError;
        return outcome;
    }

    const std::chrono::steady_clock::time_point keyClockOrigin = std::chrono::steady_clock::now();

    while (!wizard.isFinished() && !wizard.isAborted() && g_terminationSignal == 0) {
        if (!mpv.isRunning()) {
            outcome.error = "o mpv terminou inesperadamente";
            break;
        }

        const std::optional<WizardKey> key = keySource.poll(std::chrono::milliseconds(100));
        if (!key.has_value())
            continue;

        const PatternPhase previousPhase = wizard.currentPhase();
        const std::uint16_t previousCode = wizard.currentTenBitCode();
        const auto eventTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - keyClockOrigin);

        wizard.handleKeyAt(*key, eventTime);

        if (wizard.isAborted()) {
            outcome.aborted = true;
            break;
        }
        if (wizard.isFinished())
            break;

        const bool phaseChanged = wizard.currentPhase() != previousPhase;
        if (phaseChanged) {
            std::cout << phaseInstruction(wizard.currentPhase()) << "\n";
            std::cout.flush();
        }

        if (phaseChanged || wizard.currentTenBitCode() != previousCode) {
            if (const auto patternError = writeCurrentPattern(); patternError.has_value()) {
                outcome.error = patternError;
                break;
            }
            if (const auto loadError = ipc.loadFile(patternPath); loadError.has_value()) {
                outcome.error = loadError;
                break;
            }
            if (phaseChanged) {
                if (const auto instructionError = showPhaseInstruction(ipc, wizard); instructionError.has_value()) {
                    outcome.error = instructionError;
                    break;
                }
            }
            if (const auto osdError = showCurrentCode(ipc, wizard); osdError.has_value()) {
                outcome.error = osdError;
                break;
            }
        }
    }

    if (g_terminationSignal != 0) {
        outcome.aborted = true;
        outcome.terminationSignal = g_terminationSignal;
    }

    (void)ipc.quit();
    mpv.terminate();

    if (wizard.isFinished() && outcome.error == std::nullopt) {
        const std::vector<PhaseResult>& phaseResults = wizard.results();
        outcome.phaseResults = phaseResults;

        if (phaseResults.size() != 3) {
            outcome.error = "o wizard terminou sem os três resultados";
        } else {
            CalibrationProfile profile;
            profile.output = options.outputName;
            profile.minimumLuminanceNits = phaseResults.at(0).luminanceNits;
            profile.maximumLuminanceNits = std::max(1, static_cast<int>(std::lround(phaseResults.at(1).luminanceNits)));
            profile.maximumFullFrameLuminanceNits = std::max(1, static_cast<int>(std::lround(phaseResults.at(2).luminanceNits)));
            profile.timestampUtc = formatUtcTimestamp(std::chrono::system_clock::now());

            if (const auto validationError = findProfileValidationError(profile); validationError.has_value()) {
                outcome.error = "valores medidos não formam um perfil válido: " + *validationError;
            } else if (const auto saveError = saveProfileToPath(profile, options.profilePath); saveError.has_value()) {
                outcome.error = saveError;
            } else {
                outcome.savedProfilePath = options.profilePath;
                outcome.completed = true;
            }
        }
    }

    if (const auto restoreError = restoreSession(); restoreError.has_value()) {
        if (outcome.error.has_value())
            outcome.error = *outcome.error + "\nrestauração da sessão falhou: " + *restoreError;
        else
            outcome.error = "restauração da sessão falhou: " + *restoreError;
        outcome.completed = false;
    }

    return outcome;
}

} // namespace hdrcal
