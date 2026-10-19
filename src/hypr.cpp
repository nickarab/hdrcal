// Implements the hyprctl wrapper, runtime rule application and restoration guard.

#include "hdrcal/hypr.hpp"

#include "hdrcal/hypr_config.hpp"
#include "hdrcal/json.hpp"
#include "hdrcal/number_format.hpp"
#include "hdrcal/profile.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace hdrcal {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
        text.remove_suffix(1);
    return text;
}

std::string_view firstLine(std::string_view text) {
    const std::size_t lineEnd = text.find('\n');
    return trim(lineEnd == std::string_view::npos ? text : text.substr(0, lineEnd));
}

std::optional<std::string> commandFailureMessage(std::string_view action, const CommandResult& result) {
    std::string message(action);
    message += " falhou (código ";
    message += std::to_string(result.exitCode);
    message += ")";

    const std::string_view details = trim(result.standardError.empty() ? std::string_view(result.standardOutput) : std::string_view(result.standardError));
    if (!details.empty()) {
        message += ": ";
        message += details;
    }
    return message;
}

std::optional<int> parseVersionComponent(std::string_view text, std::size_t& position) {
    int value = 0;
    const auto parsed = std::from_chars(text.data() + position, text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr == text.data() + position)
        return std::nullopt;
    position = static_cast<std::size_t>(parsed.ptr - text.data());
    return value;
}

std::optional<HyprlandVersion> parseHyprlandVersionLine(std::string_view line) {
    constexpr std::string_view kPrefix = "Hyprland ";
    if (!line.starts_with(kPrefix))
        return std::nullopt;

    std::string_view versionText = line.substr(kPrefix.size());
    const std::size_t tokenEnd = versionText.find_first_of(" \t");
    if (tokenEnd != std::string_view::npos)
        versionText = versionText.substr(0, tokenEnd);

    std::size_t position = 0;
    const std::optional<int> major = parseVersionComponent(versionText, position);
    if (!major.has_value() || position >= versionText.size() || versionText[position] != '.')
        return std::nullopt;

    ++position;
    const std::optional<int> minor = parseVersionComponent(versionText, position);
    if (!minor.has_value())
        return std::nullopt;

    int patch = 0;
    if (position < versionText.size() && versionText[position] == '.') {
        ++position;
        const std::optional<int> parsedPatch = parseVersionComponent(versionText, position);
        if (!parsedPatch.has_value())
            return std::nullopt;
        patch = *parsedPatch;
    }

    // Accept build suffixes such as "-git" or "+dirty"; the numeric release is
    // the part that matters for the compatibility decision.
    if (position < versionText.size() && versionText[position] != '-' && versionText[position] != '+')
        return std::nullopt;

    return HyprlandVersion{.major = *major, .minor = *minor, .patch = patch, .raw = std::string(versionText)};
}

struct MonitorHdrStateResult {
    std::optional<MonitorHdrState> value;
    std::optional<std::string> error;
};

MonitorHdrStateResult readMonitorHdrState(const HyprController& controller, const KmsConnectorReader& kmsReader, const std::string& outputName) {
    const KmsReadResult kmsState = kmsReader(outputName);
    if (!kmsState.value)
        return {std::nullopt, kmsState.error.value_or("leitura KMS falhou")};

    const HyprMonitorListResult monitorList = controller.listMonitors();
    if (!monitorList.value)
        return {std::nullopt, monitorList.error.value_or("lista de monitores indisponível")};

    const auto monitor = std::find_if(monitorList.value->begin(), monitorList.value->end(),
                                      [&outputName](const HyprMonitorSummary& summary) { return summary.name == outputName; });
    if (monitor == monitorList.value->end())
        return {std::nullopt, "monitor '" + outputName + "' não encontrado em hyprctl monitors"};
    if (!monitor->enabled)
        return {std::nullopt, "monitor '" + outputName + "' está desativado no Hyprland"};

    return {MonitorHdrState{.maximumBitsPerComponent = kmsState.value->maximumBitsPerComponent,
                            .hdrEotf = kmsState.value->hdrEotf,
                            .currentFormat = monitor->currentFormat},
            std::nullopt};
}

std::string describeMonitorHdrState(const MonitorHdrState& state) {
    std::string description = "eotf=";
    description += state.hdrEotf.has_value() ? std::to_string(*state.hdrEotf) : "ausente";
    description += ", max bpc=";
    description += std::to_string(state.maximumBitsPerComponent);
    description += ", formato=";
    description += state.currentFormat;
    return description;
}

bool isMonitorHdrStateDegraded(const MonitorHdrState& before, const MonitorHdrState& after) {
    if (after.maximumBitsPerComponent < before.maximumBitsPerComponent)
        return true;
    if (before.hdrEotf == 2 && after.hdrEotf != 2)
        return true;
    if (before.currentFormat.find("2101010") != std::string::npos && after.currentFormat.find("2101010") == std::string::npos)
        return true;
    return false;
}

} // namespace

HyprController::HyprController(CommandExecutor executor) : m_executor(std::move(executor)) {}

HyprController HyprController::createWithSystemExecutor() {
    return HyprController([](const std::vector<std::string>& arguments) { return runCommand(arguments); });
}

HyprOptionReadResult HyprController::readOption(std::string_view optionName) const {
    const CommandResult result = m_executor({"hyprctl", "getoption", std::string(optionName)});
    if (result.exitCode != 0)
        return {std::nullopt, commandFailureMessage("hyprctl getoption " + std::string(optionName), result)};

    const std::string_view line = firstLine(result.standardOutput);
    if (line.starts_with("no such option"))
        return {std::nullopt, "opção Hyprland não encontrada: " + std::string(optionName)};

    const std::size_t separator = line.find(':');
    if (separator == std::string_view::npos)
        return {std::nullopt, "resposta inesperada de hyprctl getoption " + std::string(optionName)};

    const std::string_view typeName = trim(line.substr(0, separator));
    const std::string_view value = trim(line.substr(separator + 1));

    HyprOptionValue option;
    if (typeName == "int") {
        long long parsed = 0;
        const auto parseResult = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (parseResult.ec != std::errc{} || parseResult.ptr != value.data() + value.size())
            return {std::nullopt, "valor inteiro inválido em hyprctl getoption " + std::string(optionName)};
        option.type = HyprOptionValue::Type::Integer;
        option.integerValue = parsed;
        return {option, std::nullopt};
    }

    if (typeName == "float") {
        double parsed = 0.0;
        const auto parseResult = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (parseResult.ec != std::errc{} || parseResult.ptr != value.data() + value.size())
            return {std::nullopt, "valor decimal inválido em hyprctl getoption " + std::string(optionName)};
        option.type = HyprOptionValue::Type::FloatingPoint;
        option.floatingPointValue = parsed;
        return {option, std::nullopt};
    }

    if (typeName == "bool") {
        if (value == "true") {
            option.type = HyprOptionValue::Type::Boolean;
            option.booleanValue = true;
            return {option, std::nullopt};
        }
        if (value == "false") {
            option.type = HyprOptionValue::Type::Boolean;
            option.booleanValue = false;
            return {option, std::nullopt};
        }
        return {std::nullopt, "valor booleano inválido em hyprctl getoption " + std::string(optionName)};
    }

    if (typeName == "str" || typeName == "custom") {
        option.type = HyprOptionValue::Type::String;
        option.stringValue = std::string(value);
        return {option, std::nullopt};
    }

    return {std::nullopt, "tipo de opção Hyprland não suportado: " + std::string(typeName)};
}

HyprMonitorListResult HyprController::listMonitors() const {
    const CommandResult result = m_executor({"hyprctl", "monitors", "-j"});
    if (result.exitCode != 0)
        return {std::nullopt, commandFailureMessage("hyprctl monitors", result)};

    const JsonParseResult parsed = parseJson(result.standardOutput);
    if (!parsed.value)
        return {std::nullopt, "JSON inválido em hyprctl monitors: " + parsed.error};

    const auto* monitorArray = parsed.value->asArray();
    if (!monitorArray)
        return {std::nullopt, "hyprctl monitors não retornou um array JSON"};

    std::vector<HyprMonitorSummary> monitors;
    monitors.reserve(monitorArray->size());

    for (const JsonValue& monitorValue : *monitorArray) {
        const auto* monitorObject = monitorValue.asObject();
        if (!monitorObject)
            return {std::nullopt, "elemento de hyprctl monitors não é um objeto JSON"};

        const JsonValue* nameValue = monitorValue.find("name");
        if (!nameValue || !nameValue->asString())
            return {std::nullopt, "monitor sem campo 'name' em hyprctl monitors"};

        HyprMonitorSummary monitor;
        monitor.name = *nameValue->asString();

        if (const JsonValue* formatValue = monitorValue.find("currentFormat"); formatValue && formatValue->asString())
            monitor.currentFormat = *formatValue->asString();

        if (const JsonValue* presetValue = monitorValue.find("colorManagementPreset"); presetValue && presetValue->asString())
            monitor.colorManagementPreset = *presetValue->asString();

        if (const JsonValue* widthValue = monitorValue.find("width"); widthValue && widthValue->asNumber())
            monitor.width = static_cast<int>(*widthValue->asNumber());

        if (const JsonValue* heightValue = monitorValue.find("height"); heightValue && heightValue->asNumber())
            monitor.height = static_cast<int>(*heightValue->asNumber());

        if (const JsonValue* disabledValue = monitorValue.find("disabled"); disabledValue && disabledValue->asBoolean())
            monitor.enabled = !*disabledValue->asBoolean();
        else
            monitor.enabled = true;

        monitors.push_back(std::move(monitor));
    }

    return {std::move(monitors), std::nullopt};
}

HyprlandVersionResult HyprController::readVersion() const {
    const CommandResult result = m_executor({"hyprctl", "version"});
    if (result.exitCode != 0)
        return {std::nullopt, commandFailureMessage("hyprctl version", result)};

    const std::string_view line = firstLine(result.standardOutput);
    if (line.empty())
        return {std::nullopt, "hyprctl version não retornou saída"};

    const std::optional<HyprlandVersion> version = parseHyprlandVersionLine(line);
    if (!version.has_value())
        return {std::nullopt, "não foi possível interpretar a versão do Hyprland em: " + std::string(line)};

    return {version, std::nullopt};
}

int compareHyprlandVersions(const HyprlandVersion& left, const HyprlandVersion& right) {
    if (left.major != right.major)
        return left.major < right.major ? -1 : 1;
    if (left.minor != right.minor)
        return left.minor < right.minor ? -1 : 1;
    if (left.patch != right.patch)
        return left.patch < right.patch ? -1 : 1;
    return 0;
}

std::optional<std::string> HyprController::reload() const {
    const CommandResult result = m_executor({"hyprctl", "reload"});
    if (result.exitCode != 0)
        return commandFailureMessage("hyprctl reload", result);

    const std::string_view response = firstLine(result.standardOutput);
    if (response.starts_with("error:"))
        return "hyprctl reload retornou erro: " + std::string(response);

    return std::nullopt;
}

std::optional<std::string> HyprController::applyMonitorLuminanceRule(std::string_view outputName) const {
    if (const auto outputError = findOutputNameValidationError(outputName); outputError.has_value())
        return "saída inválida para a regra temporária: " + *outputError;

    std::string code = "hl.monitor({ output = \"";
    code += outputName;
    code += "\", min_luminance = ";
    code += formatLocaleIndependentDouble(kCalibrationMinimumLuminanceNits);
    code += ", max_luminance = ";
    code += std::to_string(kCalibrationMaximumLuminanceNits);
    code += ", max_avg_luminance = ";
    code += std::to_string(kCalibrationMaximumFullFrameLuminanceNits);
    code += " })";

    const CommandResult result = m_executor({"hyprctl", "eval", code});
    if (result.exitCode != 0)
        return commandFailureMessage("hyprctl eval", result);

    const std::string_view response = firstLine(result.standardOutput);
    if (response.starts_with("error:"))
        return "hyprctl eval retornou erro: " + std::string(response);

    return std::nullopt;
}

TemporaryLuminanceRule::TemporaryLuminanceRule(const HyprController& controller, KmsConnectorReader kmsReader,
                                               std::filesystem::path hyprlandConfigDirectory) noexcept
    : m_controller(&controller), m_kmsReader(std::move(kmsReader)), m_hyprlandConfigDirectory(std::move(hyprlandConfigDirectory)) {}

TemporaryLuminanceRule::~TemporaryLuminanceRule() {
    if (m_applied)
        (void)m_controller->reload();
}

std::optional<std::string> TemporaryLuminanceRule::apply(std::string_view outputName) {
    if (m_applied)
        return "a regra temporária de luminância já está aplicada";

    const HyprConfigLinesResult configLines = readHyprConfigLines(m_hyprlandConfigDirectory);
    if (configLines.error.has_value())
        return "não foi possível verificar as regras de monitor em " + m_hyprlandConfigDirectory.string() + ": " + *configLines.error;

    if (!findMonitorRuleLocation(configLines.lines, outputName).location.has_value()) {
        return "a configuração do Hyprland não tem uma regra de monitor com output exatamente igual a '" + std::string(outputName) +
               "'. O Hyprland 0.56.2 só preserva bitdepth/cm ao aplicar hl.monitor parcial quando já existe uma regra com esse nome; "
               "regras por 'desc:', 'make:' ou curingas seriam substituídas por padrões (8 bits, srgb) durante a calibração. "
               "Ajuste a configuração do usuário para que a regra use output = \"" +
               std::string(outputName) + "\" em " + m_hyprlandConfigDirectory.string() +
               " (por exemplo, monitors.lua). O hdrcal não altera essa configuração por você.";
    }

    const MonitorHdrStateResult before = readMonitorHdrState(*m_controller, m_kmsReader, std::string(outputName));
    if (!before.value)
        return "não foi possível registrar o estado HDR antes da regra temporária: " + before.error.value_or("leitura de estado falhou");

    if (const std::optional<std::string> applyError = m_controller->applyMonitorLuminanceRule(outputName); applyError.has_value())
        return applyError;

    m_applied = true;

    const MonitorHdrStateResult after = readMonitorHdrState(*m_controller, m_kmsReader, std::string(outputName));
    std::string verificationError;
    if (!after.value)
        verificationError = "não foi possível verificar o estado HDR depois da regra temporária: " + after.error.value_or("leitura de estado falhou");
    else if (isMonitorHdrStateDegraded(*before.value, *after.value))
        verificationError = "a regra temporária degradou o estado HDR (antes: " + describeMonitorHdrState(*before.value) +
                            "; depois: " + describeMonitorHdrState(*after.value) + ")";

    if (verificationError.empty())
        return std::nullopt;

    const std::optional<std::string> restoreError = restore();
    if (restoreError.has_value())
        return verificationError + "\na restauração da sessão falhou: " + *restoreError + "; nova tentativa no destrutor";
    return verificationError + "\na sessão foi restaurada com hyprctl reload";
}

std::optional<std::string> TemporaryLuminanceRule::restore() {
    if (!m_applied)
        return std::nullopt;

    const std::optional<std::string> error = m_controller->reload();
    if (!error.has_value())
        m_applied = false;
    return error;
}

} // namespace hdrcal
