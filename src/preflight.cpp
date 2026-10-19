// Implements the HDR precondition checks, including the conservative ICC scan.

#include "hdrcal/preflight.hpp"

#include "hdrcal/number_format.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace hdrcal {
namespace {

bool isIdentifierCharacter(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalnum(value) != 0 || character == '_';
}

// Removes Lua line comments and the simple --[[ ]] block form, preserving
// quoted strings. Long-bracket comments with additional equals signs (--[=[)
// are not interpreted; they are not used by the target configuration.
std::string stripLineComment(std::string_view line, bool luaStyle, bool& insideLuaBlockComment) {
    if (!luaStyle)
        return std::string(line.substr(0, line.find('#')));

    std::string result;
    result.reserve(line.size());

    bool insideSingleQuote = false;
    bool insideDoubleQuote = false;
    bool escaped = false;

    for (std::size_t index = 0; index < line.size();) {
        const char character = line[index];

        if (insideLuaBlockComment) {
            const std::size_t blockEnd = line.find("]]", index);
            if (blockEnd == std::string_view::npos)
                return result;
            insideLuaBlockComment = false;
            index = blockEnd + 2;
            continue;
        }

        if (escaped) {
            result.push_back(character);
            escaped = false;
            ++index;
            continue;
        }

        if ((insideSingleQuote || insideDoubleQuote) && character == '\\') {
            result.push_back(character);
            escaped = true;
            ++index;
            continue;
        }

        if (character == '\'' && !insideDoubleQuote) {
            insideSingleQuote = !insideSingleQuote;
            result.push_back(character);
            ++index;
            continue;
        }

        if (character == '"' && !insideSingleQuote) {
            insideDoubleQuote = !insideDoubleQuote;
            result.push_back(character);
            ++index;
            continue;
        }

        if (!insideSingleQuote && !insideDoubleQuote && character == '-' && index + 1 < line.size() && line[index + 1] == '-') {
            if (index + 2 < line.size() && line[index + 2] == '[') {
                const std::size_t blockEnd = line.find("]]", index + 3);
                if (blockEnd == std::string_view::npos) {
                    insideLuaBlockComment = true;
                    return result;
                }
                index = blockEnd + 2;
                continue;
            }
            break;
        }

        result.push_back(character);
        ++index;
    }

    return result;
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
    std::error_code filesystemError;
    if (!std::filesystem::exists(configDirectory, filesystemError)) {
        if (filesystemError)
            return {std::nullopt, "não foi possível verificar o diretório de configuração: " + filesystemError.message()};
        return {std::nullopt, std::nullopt};
    }

    std::vector<std::filesystem::path> configFiles;
    std::filesystem::recursive_directory_iterator iterator(configDirectory, std::filesystem::directory_options::skip_permission_denied, filesystemError);
    if (filesystemError)
        return {std::nullopt, "não foi possível percorrer o diretório de configuração: " + filesystemError.message()};

    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        const std::filesystem::directory_entry& entry = *iterator;
        std::error_code entryError;
        if (entry.is_regular_file(entryError)) {
            const std::string extension = entry.path().extension().string();
            if (extension == ".lua" || extension == ".conf")
                configFiles.push_back(entry.path());
        } else if (entryError) {
            return {std::nullopt, "não foi possível inspecionar " + entry.path().string() + ": " + entryError.message()};
        }

        iterator.increment(filesystemError);
        if (filesystemError)
            return {std::nullopt, "não foi possível percorrer o diretório de configuração: " + filesystemError.message()};
    }

    std::sort(configFiles.begin(), configFiles.end());

    for (const std::filesystem::path& configFile : configFiles) {
        std::ifstream input(configFile);
        if (!input.is_open())
            return {std::nullopt, "não foi possível ler o arquivo de configuração: " + configFile.string()};

        const bool luaStyle = configFile.extension() == ".lua";
        bool insideLuaBlockComment = false;
        std::string line;
        std::size_t lineNumber = 0;

        while (std::getline(input, line)) {
            ++lineNumber;
            const std::string relevant = stripLineComment(line, luaStyle, insideLuaBlockComment);
            const std::optional<std::string> value = findActiveIccValue(relevant);
            if (value.has_value()) {
                return {configFile.string() + ":" + std::to_string(lineNumber) + " (icc = " + *value + ")", std::nullopt};
            }
        }
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

    return result;
}

} // namespace hdrcal
