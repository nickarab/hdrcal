// Command-line interface for profile persistence, preflight checks and KMS probing.

#include "hdrcal/hypr.hpp"
#include "hdrcal/kms.hpp"
#include "hdrcal/number_format.hpp"
#include "hdrcal/preflight.hpp"
#include "hdrcal/profile.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

struct ParsedOptions {
    std::map<std::string, std::string> values;
    std::set<std::string> flags;
    std::optional<std::string> error;
};

std::optional<long long> parseIntegerArgument(std::string_view text) {
    long long value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

std::optional<double> parseDoubleArgument(std::string_view text) {
    double value = 0.0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

ParsedOptions parseOptions(const std::vector<std::string>& arguments, std::size_t start, const std::set<std::string>& valueOptions,
                           const std::set<std::string>& flagOptions) {
    ParsedOptions parsed;

    for (std::size_t index = start; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];
        if (!argument.starts_with("--")) {
            parsed.error = "argumento inesperado: " + argument;
            return parsed;
        }

        const std::size_t equalsPosition = argument.find('=');
        const std::string optionName = argument.substr(2, equalsPosition == std::string::npos ? std::string::npos : equalsPosition - 2);

        if (valueOptions.contains(optionName)) {
            if (equalsPosition != std::string::npos) {
                parsed.values[optionName] = argument.substr(equalsPosition + 1);
                continue;
            }

            if (index + 1 >= arguments.size()) {
                parsed.error = "a opção --" + optionName + " exige um valor";
                return parsed;
            }

            parsed.values[optionName] = arguments[++index];
            continue;
        }

        if (flagOptions.contains(optionName)) {
            if (equalsPosition != std::string::npos) {
                parsed.error = "a opção --" + optionName + " não aceita valor";
                return parsed;
            }
            parsed.flags.insert(optionName);
            continue;
        }

        parsed.error = "opção desconhecida: --" + optionName;
        return parsed;
    }

    return parsed;
}

std::filesystem::path defaultConfigDirectory() {
    if (const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME"); xdgConfigHome != nullptr && *xdgConfigHome != '\0')
        return std::filesystem::path(xdgConfigHome) / "hypr";

    if (const char* homeDirectory = std::getenv("HOME"); homeDirectory != nullptr && *homeDirectory != '\0')
        return std::filesystem::path(homeDirectory) / ".config" / "hypr";

    return std::filesystem::path("hypr");
}

std::filesystem::path defaultProfilePath() {
    return defaultConfigDirectory() / "hdrcal.lua";
}

void printUsage() {
    std::cout << "hdrcal — calibração visual de HDR para Hyprland\n\n";
    std::cout << "Uso:\n";
    std::cout << "  hdrcal profile write --output <nome> --min <nits> --max <nits> --avg <nits>\n";
    std::cout << "                        [--osd-brightness <0-100>] [--osd-contrast <0-100>]\n";
    std::cout << "                        [--osd-color-preset <nome>] [--path <arquivo>] [--no-reload]\n";
    std::cout << "  hdrcal preflight --output <nome> [--config-dir <diretório>]\n";
    std::cout << "  hdrcal probe --output <nome>\n";
    std::cout << "  hdrcal --help\n\n";
    std::cout << "Exemplo:\n";
    std::cout << "  hdrcal profile write --output DP-2 --min 0.02 --max 600 --avg 350\n";
}

std::optional<std::string> requireValue(const ParsedOptions& options, std::string_view name) {
    const auto iterator = options.values.find(std::string(name));
    if (iterator == options.values.end())
        return "a opção --" + std::string(name) + " é obrigatória";
    return std::nullopt;
}

int runProfileWrite(const std::vector<std::string>& arguments) {
    const ParsedOptions options =
        parseOptions(arguments, 2, {"output", "min", "max", "avg", "osd-brightness", "osd-contrast", "osd-color-preset", "path"}, {"no-reload"});
    if (options.error.has_value()) {
        std::cerr << "erro: " << *options.error << "\n";
        return 2;
    }

    for (const std::string_view required : {"output", "min", "max", "avg"}) {
        if (const auto missing = requireValue(options, required); missing.has_value()) {
            std::cerr << "erro: " << *missing << "\n";
            return 2;
        }
    }

    hdrcal::CalibrationProfile profile;
    profile.output = options.values.at("output");
    profile.timestampUtc = hdrcal::formatUtcTimestamp(std::chrono::system_clock::now());

    const std::optional<double> minimum = parseDoubleArgument(options.values.at("min"));
    const std::optional<long long> maximum = parseIntegerArgument(options.values.at("max"));
    const std::optional<long long> average = parseIntegerArgument(options.values.at("avg"));

    if (!minimum.has_value()) {
        std::cerr << "erro: --min deve ser um número decimal em nits\n";
        return 2;
    }
    if (!maximum.has_value() || *maximum < 1 || *maximum > 10000) {
        std::cerr << "erro: --max deve ser um inteiro entre 1 e 10000 nits\n";
        return 2;
    }
    if (!average.has_value() || *average < 1 || *average > 10000) {
        std::cerr << "erro: --avg deve ser um inteiro entre 1 e 10000 nits\n";
        return 2;
    }

    profile.minimumLuminanceNits = *minimum;
    profile.maximumLuminanceNits = static_cast<int>(*maximum);
    profile.maximumFullFrameLuminanceNits = static_cast<int>(*average);

    const bool hasBrightness = options.values.contains("osd-brightness");
    const bool hasContrast = options.values.contains("osd-contrast");
    const bool hasPreset = options.values.contains("osd-color-preset");
    if (hasBrightness || hasContrast || hasPreset) {
        if (!hasBrightness || !hasContrast || !hasPreset) {
            std::cerr << "erro: o snapshot do OSD exige --osd-brightness, --osd-contrast e --osd-color-preset juntos\n";
            return 2;
        }

        const std::optional<long long> brightness = parseIntegerArgument(options.values.at("osd-brightness"));
        const std::optional<long long> contrast = parseIntegerArgument(options.values.at("osd-contrast"));
        if (!brightness.has_value() || *brightness < 0 || *brightness > 100) {
            std::cerr << "erro: --osd-brightness deve ser um inteiro entre 0 e 100\n";
            return 2;
        }
        if (!contrast.has_value() || *contrast < 0 || *contrast > 100) {
            std::cerr << "erro: --osd-contrast deve ser um inteiro entre 0 e 100\n";
            return 2;
        }

        profile.osdSnapshot = hdrcal::MonitorOsdSnapshot{
            .brightness = static_cast<int>(*brightness), .contrast = static_cast<int>(*contrast), .colorPreset = options.values.at("osd-color-preset")};
    }

    const std::filesystem::path profilePath = options.values.contains("path") ? std::filesystem::path(options.values.at("path")) : defaultProfilePath();
    if (const auto saveError = hdrcal::saveProfileToPath(profile, profilePath); saveError.has_value()) {
        std::cerr << "erro: " << *saveError << "\n";
        return 1;
    }

    std::cout << "Perfil gravado em " << profilePath.string() << "\n";
    std::cout << "  saída: " << profile.output << "\n";
    std::cout << "  min: " << hdrcal::formatLocaleIndependentDouble(profile.minimumLuminanceNits) << " nits\n";
    std::cout << "  max: " << profile.maximumLuminanceNits << " nits\n";
    std::cout << "  max full-frame: " << profile.maximumFullFrameLuminanceNits << " nits\n";

    if (!options.flags.contains("no-reload")) {
        const hdrcal::HyprController controller = hdrcal::HyprController::createWithSystemExecutor();
        if (const auto reloadError = controller.reload(); reloadError.has_value()) {
            std::cerr << "aviso: perfil salvo, mas hyprctl reload falhou: " << *reloadError << "\n";
            return 1;
        }
        std::cout << "hyprctl reload: ok\n";
    }

    return 0;
}

int runPreflight(const std::vector<std::string>& arguments) {
    const ParsedOptions options = parseOptions(arguments, 1, {"output", "config-dir"}, {});
    if (options.error.has_value()) {
        std::cerr << "erro: " << *options.error << "\n";
        return 2;
    }
    if (const auto missing = requireValue(options, "output"); missing.has_value()) {
        std::cerr << "erro: " << *missing << "\n";
        return 2;
    }

    const std::filesystem::path configDirectory =
        options.values.contains("config-dir") ? std::filesystem::path(options.values.at("config-dir")) : defaultConfigDirectory();
    const hdrcal::HyprController controller = hdrcal::HyprController::createWithSystemExecutor();
    const hdrcal::PreflightResult result =
        hdrcal::checkHdrPreconditions(controller, options.values.at("output"), hdrcal::readKmsConnectorState, configDirectory);

    for (const hdrcal::PreflightCheck& check : result.checks)
        std::cout << (check.passed ? "[PASS] " : "[FAIL] ") << check.name << " — " << check.detail << "\n";

    return result.allPassed() ? 0 : 1;
}

int runProbe(const std::vector<std::string>& arguments) {
    const ParsedOptions options = parseOptions(arguments, 1, {"output"}, {});
    if (options.error.has_value()) {
        std::cerr << "erro: " << *options.error << "\n";
        return 2;
    }
    if (const auto missing = requireValue(options, "output"); missing.has_value()) {
        std::cerr << "erro: " << *missing << "\n";
        return 2;
    }

    const std::string& outputName = options.values.at("output");
    const hdrcal::KmsReadResult kmsState = hdrcal::readKmsConnectorState(outputName);
    if (!kmsState.value) {
        std::cerr << "erro: " << kmsState.error.value_or("leitura KMS falhou") << "\n";
        return 1;
    }

    const hdrcal::HyprController controller = hdrcal::HyprController::createWithSystemExecutor();
    const hdrcal::HyprMonitorListResult monitors = controller.listMonitors();

    std::cout << "KMS " << outputName << ":\n";
    std::cout << "  max bpc: " << kmsState.value->maximumBitsPerComponent << "\n";
    std::cout << "  eotf: " << (kmsState.value->hdrEotf.has_value() ? std::to_string(*kmsState.value->hdrEotf) : std::string("ausente")) << "\n";
    std::cout << "  max_cll: " << kmsState.value->maximumContentLightLevel << "\n";
    std::cout << "  max_fall: " << kmsState.value->maximumFrameAverageLightLevel << "\n";

    if (monitors.value) {
        const auto monitor = std::find_if(monitors.value->begin(), monitors.value->end(),
                                          [&outputName](const hdrcal::HyprMonitorSummary& summary) { return summary.name == outputName; });
        if (monitor != monitors.value->end()) {
            std::cout << "Hyprland " << outputName << ":\n";
            std::cout << "  currentFormat: " << monitor->currentFormat << "\n";
            std::cout << "  colorManagementPreset: " << monitor->colorManagementPreset << "\n";
        }
    }

    return 0;
}

} // namespace

int main(int argumentCount, char** argumentValues) {
    const std::vector<std::string> arguments(argumentValues + 1, argumentValues + argumentCount);

    if (arguments.empty() || arguments[0] == "--help" || arguments[0] == "-h") {
        printUsage();
        return 0;
    }

    if (arguments[0] == "profile" && arguments.size() >= 2 && arguments[1] == "write")
        return runProfileWrite(arguments);

    if (arguments[0] == "preflight")
        return runPreflight(arguments);

    if (arguments[0] == "probe")
        return runProbe(arguments);

    std::cerr << "erro: comando inválido\n\n";
    printUsage();
    return 2;
}
