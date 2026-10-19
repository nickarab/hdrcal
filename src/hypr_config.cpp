// Implements the read-only Hyprland configuration scan used by preflight and
// by the temporary luminance rule guard.

#include "hdrcal/hypr_config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace hdrcal {
namespace {

bool isIdentifierCharacter(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalnum(value) != 0 || character == '_';
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
        text.remove_suffix(1);
    return text;
}

// Removes Lua line comments and the simple --[[ ]] block form, preserving
// quoted strings. Long-bracket comments with additional equals signs (--[=[)
// are not interpreted; they are not used by the target configuration.
std::string stripConfigComment(std::string_view line, bool luaStyle, bool& insideLuaBlockComment) {
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

bool isQuote(char character) {
    return character == '"' || character == '\'';
}

std::size_t skipQuotedString(std::string_view text, std::size_t position) {
    if (position >= text.size() || !isQuote(text[position]))
        return position;

    const char quote = text[position];
    ++position;

    while (position < text.size()) {
        if (text[position] == '\\' && position + 1 < text.size()) {
            position += 2;
            continue;
        }
        if (text[position] == quote)
            return position + 1;
        ++position;
    }

    return position;
}

bool matchesIdentifier(std::string_view text, std::size_t position, std::string_view identifier) {
    if (position + identifier.size() > text.size())
        return false;
    if (text.substr(position, identifier.size()) != identifier)
        return false;
    if (position > 0 && isIdentifierCharacter(text[position - 1]))
        return false;
    if (position + identifier.size() < text.size() && isIdentifierCharacter(text[position + identifier.size()]))
        return false;
    return true;
}

std::size_t skipWhitespace(std::string_view text, std::size_t position) {
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) != 0)
        ++position;
    return position;
}

struct QuotedValue {
    std::string value;
    std::size_t endPosition = 0;
};

std::optional<QuotedValue> readQuotedValue(std::string_view text, std::size_t position) {
    if (position >= text.size() || !isQuote(text[position]))
        return std::nullopt;

    const char quote = text[position];
    QuotedValue result;
    result.endPosition = position + 1;

    while (result.endPosition < text.size()) {
        const char character = text[result.endPosition];
        if (character == '\\' && result.endPosition + 1 < text.size()) {
            result.value.push_back(text[result.endPosition + 1]);
            result.endPosition += 2;
            continue;
        }
        if (character == quote) {
            ++result.endPosition;
            return result;
        }
        result.value.push_back(character);
        ++result.endPosition;
    }

    return std::nullopt;
}

std::optional<std::string> findLuaMonitorOutputInFile(std::string_view content, const std::vector<std::size_t>& lineStarts,
                                                      const std::vector<std::size_t>& lineNumbers, std::string_view outputName) {
    auto lineLocation = [&](std::size_t offset) -> std::string {
        const auto iterator = std::upper_bound(lineStarts.begin(), lineStarts.end(), offset);
        const std::size_t index = iterator == lineStarts.begin() ? 0 : static_cast<std::size_t>(iterator - lineStarts.begin() - 1);
        return std::to_string(lineNumbers.at(index));
    };

    std::size_t position = 0;
    while (position < content.size()) {
        if (isQuote(content[position])) {
            position = skipQuotedString(content, position);
            continue;
        }

        if (!matchesIdentifier(content, position, "hl")) {
            ++position;
            continue;
        }

        std::size_t cursor = position + 2;
        cursor = skipWhitespace(content, cursor);
        if (cursor >= content.size() || content[cursor] != '.') {
            ++position;
            continue;
        }

        ++cursor;
        cursor = skipWhitespace(content, cursor);
        if (!matchesIdentifier(content, cursor, "monitor")) {
            ++position;
            continue;
        }

        const std::size_t callStart = position;
        cursor = skipWhitespace(content, cursor + 7);
        if (cursor < content.size() && content[cursor] == '(') {
            ++cursor;
            cursor = skipWhitespace(content, cursor);
        }

        if (cursor >= content.size() || content[cursor] != '{') {
            position = cursor;
            continue;
        }

        ++cursor;
        int braceDepth = 1;
        bool foundOutput = false;

        while (cursor < content.size() && braceDepth > 0) {
            const char character = content[cursor];

            if (isQuote(character)) {
                cursor = skipQuotedString(content, cursor);
                continue;
            }

            if (character == '{') {
                ++braceDepth;
                ++cursor;
                continue;
            }

            if (character == '}') {
                --braceDepth;
                ++cursor;
                continue;
            }

            if (braceDepth == 1 && matchesIdentifier(content, cursor, "output")) {
                std::size_t valuePosition = skipWhitespace(content, cursor + 6);
                if (valuePosition < content.size() && content[valuePosition] == '=') {
                    valuePosition = skipWhitespace(content, valuePosition + 1);
                    if (const std::optional<QuotedValue> value = readQuotedValue(content, valuePosition); value.has_value()) {
                        cursor = value->endPosition;
                        if (value->value == outputName)
                            foundOutput = true;
                        continue;
                    }
                }
            }

            ++cursor;
        }

        if (foundOutput)
            return lineLocation(callStart);

        position = cursor;
    }

    return std::nullopt;
}

bool isLegacyMonitorRuleForOutput(std::string_view line, std::string_view outputName) {
    const std::string_view trimmed = trim(line);
    constexpr std::string_view kMonitorKeyword = "monitor";
    if (!trimmed.starts_with(kMonitorKeyword))
        return false;

    std::size_t cursor = kMonitorKeyword.size();
    if (cursor < trimmed.size() && isIdentifierCharacter(trimmed[cursor]))
        return false;

    cursor = skipWhitespace(trimmed, cursor);
    if (cursor >= trimmed.size() || trimmed[cursor] != '=')
        return false;

    cursor = skipWhitespace(trimmed, cursor + 1);
    const std::size_t selectorStart = cursor;
    while (cursor < trimmed.size() && trimmed[cursor] != ',')
        ++cursor;

    return trim(trimmed.substr(selectorStart, cursor - selectorStart)) == outputName;
}

} // namespace

HyprConfigLinesResult readHyprConfigLines(const std::filesystem::path& configDirectory) {
    HyprConfigLinesResult result;

    std::error_code filesystemError;
    if (!std::filesystem::exists(configDirectory, filesystemError)) {
        if (filesystemError)
            result.error = "não foi possível verificar o diretório de configuração: " + filesystemError.message();
        return result;
    }

    std::vector<std::filesystem::path> configFiles;
    std::filesystem::recursive_directory_iterator iterator(configDirectory, std::filesystem::directory_options::skip_permission_denied, filesystemError);
    if (filesystemError) {
        result.error = "não foi possível percorrer o diretório de configuração: " + filesystemError.message();
        return result;
    }

    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        const std::filesystem::directory_entry& entry = *iterator;
        std::error_code entryError;
        if (entry.is_regular_file(entryError)) {
            const std::string extension = entry.path().extension().string();
            if (extension == ".lua" || extension == ".conf")
                configFiles.push_back(entry.path());
        } else if (entryError) {
            result.error = "não foi possível inspecionar " + entry.path().string() + ": " + entryError.message();
            return result;
        }

        iterator.increment(filesystemError);
        if (filesystemError) {
            result.error = "não foi possível percorrer o diretório de configuração: " + filesystemError.message();
            return result;
        }
    }

    std::sort(configFiles.begin(), configFiles.end());

    for (const std::filesystem::path& configFile : configFiles) {
        std::ifstream input(configFile);
        if (!input.is_open()) {
            result.error = "não foi possível ler o arquivo de configuração: " + configFile.string();
            return result;
        }

        const bool luaStyle = configFile.extension() == ".lua";
        bool insideLuaBlockComment = false;
        std::string line;
        std::size_t lineNumber = 0;

        while (std::getline(input, line)) {
            ++lineNumber;
            result.lines.push_back(
                HyprConfigLine{.filePath = configFile, .lineNumber = lineNumber, .text = stripConfigComment(line, luaStyle, insideLuaBlockComment)});
        }

        if (input.bad()) {
            result.error = "falha de leitura no arquivo de configuração: " + configFile.string();
            return result;
        }
    }

    return result;
}

MonitorRuleLocation findMonitorRuleLocation(const std::vector<HyprConfigLine>& lines, std::string_view outputName) {
    std::size_t index = 0;

    while (index < lines.size()) {
        const std::filesystem::path filePath = lines[index].filePath;

        std::string content;
        std::vector<std::size_t> lineStarts;
        std::vector<std::size_t> lineNumbers;

        while (index < lines.size() && lines[index].filePath == filePath) {
            lineStarts.push_back(content.size());
            lineNumbers.push_back(lines[index].lineNumber);
            content += lines[index].text;
            content += '\n';
            ++index;
        }

        if (filePath.extension() == ".lua") {
            const std::optional<std::string> line = findLuaMonitorOutputInFile(content, lineStarts, lineNumbers, outputName);
            if (line.has_value())
                return {filePath.string() + ":" + *line, std::nullopt};
            continue;
        }

        for (std::size_t lineIndex = 0; lineIndex < lineNumbers.size(); ++lineIndex) {
            const std::size_t start = lineStarts[lineIndex];
            const std::size_t end = lineIndex + 1 < lineStarts.size() ? lineStarts[lineIndex + 1] : content.size();
            if (isLegacyMonitorRuleForOutput(std::string_view(content).substr(start, end - start), outputName))
                return {filePath.string() + ":" + std::to_string(lineNumbers[lineIndex]), std::nullopt};
        }
    }

    return {std::nullopt, std::nullopt};
}

} // namespace hdrcal
