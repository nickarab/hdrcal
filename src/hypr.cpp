// Implements the hyprctl wrapper, runtime rule application and restoration guard.

#include "hdrcal/hypr.hpp"

#include "hdrcal/json.hpp"
#include "hdrcal/number_format.hpp"
#include "hdrcal/profile.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>
#include <string_view>
#include <system_error>

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

        if (const JsonValue* disabledValue = monitorValue.find("disabled"); disabledValue && disabledValue->asBoolean())
            monitor.enabled = !*disabledValue->asBoolean();
        else
            monitor.enabled = true;

        monitors.push_back(std::move(monitor));
    }

    return {std::move(monitors), std::nullopt};
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

TemporaryLuminanceRule::TemporaryLuminanceRule(const HyprController& controller) noexcept : m_controller(&controller) {}

TemporaryLuminanceRule::~TemporaryLuminanceRule() {
    if (m_applied)
        (void)m_controller->reload();
}

std::optional<std::string> TemporaryLuminanceRule::apply(std::string_view outputName) {
    const std::optional<std::string> error = m_controller->applyMonitorLuminanceRule(outputName);
    if (!error.has_value())
        m_applied = true;
    return error;
}

std::optional<std::string> TemporaryLuminanceRule::restore() {
    const std::optional<std::string> error = m_controller->reload();
    m_applied = false;
    return error;
}

} // namespace hdrcal
