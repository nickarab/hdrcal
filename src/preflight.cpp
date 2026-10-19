// Implements the HDR precondition checks, including the conservative ICC scan.

#include "hdrcal/preflight.hpp"

#include "hdrcal/hypr_config.hpp"
#include "hdrcal/number_format.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hdrcal {
namespace {

bool isIdentifierCharacter(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalnum(value) != 0 || character == '_';
}

std::optional<std::string> findActiveIccValue(std::string_view line) {
    std::size_t searchPosition = 0;

    while ((searchPosition = line.find("icc", searchPosition)) != std::string_view::npos) {
        const bool leftBoundary = searchPosition == 0 || !isIdentifierCharacter(line[searchPosition - 1]);
        const std::size_t after = searchPosition + 3;
        const bool rightBoundary = after >= line.size() || !isIdentifierCharacter(line[after]);

        if (!leftBoundary || !rightBoundary) {
            searchPosition = after;
            continue;
        }

        std::size_t cursor = after;
        while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])) != 0)
            ++cursor;

        if (cursor < line.size() && line[cursor] == '=') {
            ++cursor;
            while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])) != 0)
                ++cursor;

            if (cursor < line.size() && (line[cursor] == '"' || line[cursor] == '\'')) {
                const char quote = line[cursor++];
                const std::size_t start = cursor;
                bool escaped = false;

                while (cursor < line.size()) {
                    if (escaped) {
                        escaped = false;
                    } else if (line[cursor] == '\\') {
                        escaped = true;
                    } else if (line[cursor] == quote) {
                        break;
                    }
                    ++cursor;
                }

                if (cursor > start)
                    return std::string(line.substr(start, cursor - start));
            } else {
                const std::size_t start = cursor;
                while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])) == 0 && line[cursor] != ',' && line[cursor] != '}')
                    ++cursor;

                const std::string_view value = line.substr(start, cursor - start);
                if (!value.empty() && value != "nil" && value != "false")
                    return std::string(value);
            }
        } else if (cursor < line.size() && line[cursor] == ',') {
            ++cursor;
            while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])) != 0)
                ++cursor;

            const std::size_t start = cursor;
            while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])) == 0 && line[cursor] != ',')
                ++cursor;

            if (cursor > start)
                return std::string(line.substr(start, cursor - start));
        }

        searchPosition = after;
    }

    return std::nullopt;
}

struct IccScanResult {
    std::optional<std::string> activeLocation;
    std::optional<std::string> error;
};

IccScanResult scanConfigDirectoryForActiveIcc(const std::filesystem::path& configDirectory) {
    const HyprConfigLinesResult configLines = readHyprConfigLines(configDirectory);
    if (configLines.error.has_value())
        return {std::nullopt, *configLines.error};

    for (const HyprConfigLine& line : configLines.lines) {
        const std::optional<std::string> value = findActiveIccValue(line.text);
        if (value.has_value())
            return {line.filePath.string() + ":" + std::to_string(line.lineNumber) + " (icc = " + *value + ")", std::nullopt};
    }

    return {std::nullopt, std::nullopt};
}

bool optionIsDisabled(const HyprOptionValue& option) {
    switch (option.type) {
    case HyprOptionValue::Type::Integer:
        return option.integerValue == 0;
    case HyprOptionValue::Type::Boolean:
        return !option.booleanValue;
    case HyprOptionValue::Type::FloatingPoint:
        return option.floatingPointValue == 0.0;
    case HyprOptionValue::Type::String:
        return false;
    }
    return false;
}

std::string describeOptionValue(const HyprOptionValue& option) {
    switch (option.type) {
    case HyprOptionValue::Type::Integer:
        return std::to_string(option.integerValue);
    case HyprOptionValue::Type::Boolean:
        return option.booleanValue ? "true" : "false";
    case HyprOptionValue::Type::FloatingPoint:
        return formatLocaleIndependentDouble(option.floatingPointValue);
    case HyprOptionValue::Type::String:
        return option.stringValue;
    }
    return {};
}

PreflightCheck makeCheck(std::string name, bool passed, std::string detail) {
    return PreflightCheck{.name = std::move(name), .passed = passed, .detail = std::move(detail)};
}

} // namespace

bool PreflightResult::allPassed() const {
    return std::all_of(checks.begin(), checks.end(), [](const PreflightCheck& check) { return check.passed; });
}

PreflightResult checkHdrPreconditions(const HyprController& controller, const std::string& outputName, const KmsConnectorReader& kmsReader,
                                      const std::filesystem::path& hyprlandConfigDirectory) {
    PreflightResult result;

    const KmsReadResult kmsState = kmsReader(outputName);
    const HyprMonitorListResult monitorList = controller.listMonitors();

    if (!kmsState.value) {
        result.checks.push_back(makeCheck("HDR ativo no KMS (eotf = 2)", false, kmsState.error.value_or("leitura KMS falhou")));
    } else if (!kmsState.value->hdrEotf.has_value()) {
        result.checks.push_back(makeCheck("HDR ativo no KMS (eotf = 2)", false, "HDR_OUTPUT_METADATA ausente no conector " + outputName));
    } else if (*kmsState.value->hdrEotf != 2) {
        result.checks.push_back(makeCheck("HDR ativo no KMS (eotf = 2)", false, "eotf=" + std::to_string(*kmsState.value->hdrEotf) + ", esperado 2 (PQ)"));
    } else {
        result.checks.push_back(makeCheck("HDR ativo no KMS (eotf = 2)", true, "eotf=2 (PQ) no conector " + outputName));
    }

    std::string framebufferFormat;
    std::optional<std::string> monitorError;
    if (!monitorList.value) {
        monitorError = monitorList.error.value_or("lista de monitores indisponível");
    } else {
        const auto monitor = std::find_if(monitorList.value->begin(), monitorList.value->end(),
                                          [&outputName](const HyprMonitorSummary& summary) { return summary.name == outputName; });
        if (monitor == monitorList.value->end())
            monitorError = "monitor '" + outputName + "' não encontrado em hyprctl monitors";
        else if (!monitor->enabled)
            monitorError = "monitor '" + outputName + "' está desativado no Hyprland";
        else
            framebufferFormat = monitor->currentFormat;
    }

    if (!kmsState.value) {
        result.checks.push_back(makeCheck("Saída em 10 bits (max bpc e framebuffer)", false, kmsState.error.value_or("leitura KMS falhou")));
    } else if (monitorError.has_value()) {
        result.checks.push_back(makeCheck("Saída em 10 bits (max bpc e framebuffer)", false, *monitorError));
    } else {
        const bool bitsPerComponentOk = kmsState.value->maximumBitsPerComponent == 10;
        const bool framebufferOk = framebufferFormat.find("2101010") != std::string::npos;

        std::string detail = "max bpc=" + std::to_string(kmsState.value->maximumBitsPerComponent) + ", framebuffer=" + framebufferFormat;
        result.checks.push_back(makeCheck("Saída em 10 bits (max bpc e framebuffer)", bitsPerComponentOk && framebufferOk, std::move(detail)));
    }

    const HyprOptionReadResult automaticHdr = controller.readOption("render:cm_auto_hdr");
    if (!automaticHdr.value)
        result.checks.push_back(makeCheck("Troca automática de HDR desligada", false, automaticHdr.error.value_or("leitura da opção falhou")));
    else
        result.checks.push_back(makeCheck("Troca automática de HDR desligada", optionIsDisabled(*automaticHdr.value),
                                          "render:cm_auto_hdr=" + describeOptionValue(*automaticHdr.value) + ", esperado 0"));

    const IccScanResult iccScan = scanConfigDirectoryForActiveIcc(hyprlandConfigDirectory);
    if (iccScan.error.has_value())
        result.checks.push_back(makeCheck("Nenhum ICC ativo no monitor", false, *iccScan.error));
    else if (iccScan.activeLocation.has_value())
        result.checks.push_back(makeCheck("Nenhum ICC ativo no monitor", false, "ICC configurado em " + *iccScan.activeLocation));
    else
        result.checks.push_back(makeCheck("Nenhum ICC ativo no monitor", true, "nenhum 'icc' de monitor encontrado em " + hyprlandConfigDirectory.string()));

    const HyprOptionReadResult directScanout = controller.readOption("render:direct_scanout");
    if (!directScanout.value)
        result.checks.push_back(makeCheck("Direct scanout desligado", false, directScanout.error.value_or("leitura da opção falhou")));
    else
        result.checks.push_back(makeCheck("Direct scanout desligado", optionIsDisabled(*directScanout.value),
                                          "render:direct_scanout=" + describeOptionValue(*directScanout.value) + ", esperado 0"));

    const HyprlandVersionResult version = controller.readVersion();
    if (!version.value) {
        result.checks.push_back(makeCheck("Versão do Hyprland compatível (>= 0.55)", false, version.error.value_or("hyprctl version falhou")));
    } else {
        const HyprlandVersion floorVersion{.major = kMinimumHyprlandMajorVersion, .minor = kMinimumHyprlandMinorVersion, .patch = 0, .raw = {}};
        const HyprlandVersion auditedVersion{
            .major = kAuditedHyprlandMajorVersion, .minor = kAuditedHyprlandMinorVersion, .patch = kAuditedHyprlandPatchVersion, .raw = {}};

        if (compareHyprlandVersions(*version.value, floorVersion) < 0) {
            result.checks.push_back(
                makeCheck("Versão do Hyprland compatível (>= 0.55)", false,
                          "Hyprland " + version.value->raw + " é anterior ao mínimo 0.55 exigido pela configuração Lua e por hyprctl eval"));
        } else {
            std::string detail = "Hyprland " + version.value->raw;
            if (compareHyprlandVersions(*version.value, auditedVersion) > 0)
                detail += " — versão mais nova que a auditada 0.56.2; recursos novos podem mudar o comportamento";
            else
                detail += " (auditada: 0.56.2)";
            result.checks.push_back(makeCheck("Versão do Hyprland compatível (>= 0.55)", true, std::move(detail)));
        }
    }

    return result;
}

} // namespace hdrcal
