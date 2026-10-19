// Implements the pure Lua monitor-rule editor and the transactional writer used
// by the explicit `hdrcal integrate` command.

#include "hdrcal/integrate.hpp"

#include "hdrcal/atomic_file.hpp"
#include "hdrcal/profile.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace hdrcal {
namespace {

bool isIdentifierCharacter(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalnum(value) != 0 || character == '_';
}

bool isQuote(char character) {
    return character == '"' || character == '\'';
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

std::size_t skipQuotedString(std::string_view text, std::size_t position) {
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

struct LongBracket {
    std::size_t contentStart = 0;
    std::string closing;
};

std::optional<LongBracket> readLongBracketOpening(std::string_view text, std::size_t position) {
    if (position >= text.size() || text[position] != '[')
        return std::nullopt;

    std::size_t cursor = position + 1;
    while (cursor < text.size() && text[cursor] == '=')
        ++cursor;

    if (cursor >= text.size() || text[cursor] != '[')
        return std::nullopt;

    LongBracket bracket;
    bracket.contentStart = cursor + 1;
    bracket.closing = "]" + std::string(cursor - position - 1, '=') + "]";
    return bracket;
}

// Skips one quoted string or comment starting at `position`. Returns true when
// it consumed something, so callers can keep scanning the remaining code.
bool skipNonCode(std::string_view text, std::size_t& position) {
    if (position >= text.size())
        return false;

    if (isQuote(text[position])) {
        position = skipQuotedString(text, position);
        return true;
    }

    if (text[position] == '-' && position + 1 < text.size() && text[position + 1] == '-') {
        if (position + 2 < text.size() && text[position + 2] == '[') {
            if (const std::optional<LongBracket> bracket = readLongBracketOpening(text, position + 2); bracket.has_value()) {
                const std::size_t closing = text.find(bracket->closing, bracket->contentStart);
                position = closing == std::string_view::npos ? text.size() : closing + bracket->closing.size();
                return true;
            }
        }

        const std::size_t end = text.find('\n', position + 2);
        position = end == std::string_view::npos ? text.size() : end + 1;
        return true;
    }

    if (text[position] == '[') {
        if (const std::optional<LongBracket> bracket = readLongBracketOpening(text, position); bracket.has_value()) {
            const std::size_t closing = text.find(bracket->closing, bracket->contentStart);
            position = closing == std::string_view::npos ? text.size() : closing + bracket->closing.size();
            return true;
        }
    }

    return false;
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

struct LuaMonitorCall {
    std::size_t callStart = 0;
    std::size_t tableOpen = 0;
    std::size_t tableClose = 0;
};

std::size_t findMatchingBrace(std::string_view text, std::size_t openPosition) {
    int depth = 1;
    std::size_t position = openPosition + 1;

    while (position < text.size()) {
        if (skipNonCode(text, position))
            continue;

        if (text[position] == '{') {
            ++depth;
            ++position;
            continue;
        }
        if (text[position] == '}') {
            --depth;
            if (depth == 0)
                return position;
            ++position;
            continue;
        }
        ++position;
    }

    return std::string_view::npos;
}

bool tableNamesOutput(std::string_view text, const LuaMonitorCall& call, std::string_view outputName) {
    int depth = 1;
    std::size_t position = call.tableOpen + 1;

    while (position < call.tableClose) {
        if (skipNonCode(text, position))
            continue;

        if (text[position] == '{') {
            ++depth;
            ++position;
            continue;
        }
        if (text[position] == '}') {
            --depth;
            ++position;
            continue;
        }

        if (depth == 1 && matchesIdentifier(text, position, "output")) {
            std::size_t valuePosition = skipWhitespace(text, position + 6);
            if (valuePosition < call.tableClose && text[valuePosition] == '=') {
                valuePosition = skipWhitespace(text, valuePosition + 1);
                if (const std::optional<QuotedValue> value = readQuotedValue(text, valuePosition); value.has_value()) {
                    position = value->endPosition;
                    if (value->value == outputName)
                        return true;
                    continue;
                }
            }
        }

        ++position;
    }

    return false;
}

std::vector<LuaMonitorCall> findMatchingLuaMonitorCalls(std::string_view text, std::string_view outputName) {
    std::vector<LuaMonitorCall> calls;
    std::size_t position = 0;

    while (position < text.size()) {
        if (skipNonCode(text, position))
            continue;

        if (!matchesIdentifier(text, position, "hl")) {
            ++position;
            continue;
        }

        std::size_t cursor = skipWhitespace(text, position + 2);
        if (cursor >= text.size() || text[cursor] != '.') {
            ++position;
            continue;
        }

        ++cursor;
        cursor = skipWhitespace(text, cursor);
        if (!matchesIdentifier(text, cursor, "monitor")) {
            ++position;
            continue;
        }

        const std::size_t callStart = position;
        cursor = skipWhitespace(text, cursor + 7);
        if (cursor < text.size() && text[cursor] == '(') {
            ++cursor;
            cursor = skipWhitespace(text, cursor);
        }

        if (cursor >= text.size() || text[cursor] != '{') {
            position = cursor;
            continue;
        }

        const std::size_t tableOpen = cursor;
        const std::size_t tableClose = findMatchingBrace(text, tableOpen);
        if (tableClose == std::string_view::npos) {
            position = tableOpen + 1;
            continue;
        }

        const LuaMonitorCall call{.callStart = callStart, .tableOpen = tableOpen, .tableClose = tableClose};
        if (tableNamesOutput(text, call, outputName))
            calls.push_back(call);

        position = tableClose + 1;
    }

    return calls;
}

struct TableFields {
    bool minimum = false;
    bool maximum = false;
    bool maximumFullFrame = false;
};

TableFields findTopLevelFields(std::string_view text, const LuaMonitorCall& call) {
    TableFields fields;
    int depth = 1;
    std::size_t position = call.tableOpen + 1;

    while (position < call.tableClose) {
        if (skipNonCode(text, position))
            continue;

        if (text[position] == '{') {
            ++depth;
            ++position;
            continue;
        }
        if (text[position] == '}') {
            --depth;
            ++position;
            continue;
        }

        if (depth == 1) {
            if (matchesIdentifier(text, position, "min_luminance"))
                fields.minimum = true;
            else if (matchesIdentifier(text, position, "max_avg_luminance"))
                fields.maximumFullFrame = true;
            else if (matchesIdentifier(text, position, "max_luminance"))
                fields.maximum = true;
        }

        ++position;
    }

    return fields;
}

std::size_t lineStartBefore(std::string_view text, std::size_t position) {
    const std::size_t newline = text.rfind('\n', position == 0 ? 0 : position - 1);
    if (newline == std::string_view::npos)
        return 0;
    return newline + 1;
}

std::string detectFieldIndent(std::string_view text, const LuaMonitorCall& call, std::string_view closeIndent) {
    std::size_t lineStart = text.find('\n', call.tableOpen);
    if (lineStart == std::string_view::npos || lineStart > call.tableClose)
        return std::string(closeIndent) + "    ";

    ++lineStart;
    while (lineStart < call.tableClose) {
        const std::size_t lineEnd = text.find('\n', lineStart);
        const std::size_t effectiveEnd = lineEnd == std::string_view::npos || lineEnd > call.tableClose ? call.tableClose : lineEnd;
        const std::string_view line = text.substr(lineStart, effectiveEnd - lineStart);
        const std::size_t firstNonSpace = line.find_first_not_of(" \t");
        if (firstNonSpace != std::string_view::npos) {
            if (lineStart + firstNonSpace != call.tableClose)
                return std::string(line.substr(0, firstNonSpace));
            break;
        }

        if (lineEnd == std::string_view::npos)
            break;
        lineStart = lineEnd + 1;
    }

    return std::string(closeIndent) + "    ";
}

std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t start = 0;

    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            lines.emplace_back(text.substr(start));
            break;
        }
        lines.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }

    return lines;
}

enum class DiffKind {
    Context,
    Delete,
    Insert,
};

struct DiffEntry {
    DiffKind kind = DiffKind::Context;
    std::string text;
};

std::vector<DiffEntry> buildLineEditScript(const std::vector<std::string>& oldLines, const std::vector<std::string>& newLines) {
    std::vector<DiffEntry> entries;
    std::size_t oldIndex = 0;
    std::size_t newIndex = 0;

    while (oldIndex < oldLines.size() || newIndex < newLines.size()) {
        if (oldIndex < oldLines.size() && newIndex < newLines.size() && oldLines[oldIndex] == newLines[newIndex]) {
            entries.push_back({.kind = DiffKind::Context, .text = oldLines[oldIndex]});
            ++oldIndex;
            ++newIndex;
            continue;
        }

        // The editor only inserts lines, but keep a deletion fallback so the
        // diff stays exact if the content ever changes differently.
        if (oldIndex < oldLines.size()) {
            const auto nextMatch = std::find(newLines.begin() + static_cast<std::ptrdiff_t>(newIndex), newLines.end(), oldLines[oldIndex]);
            if (nextMatch != newLines.end()) {
                while (newIndex < static_cast<std::size_t>(nextMatch - newLines.begin())) {
                    entries.push_back({.kind = DiffKind::Insert, .text = newLines[newIndex]});
                    ++newIndex;
                }
                continue;
            }
        }

        if (newIndex < newLines.size() && (oldIndex >= oldLines.size() || oldLines[oldIndex] != newLines[newIndex])) {
            if (oldIndex < oldLines.size() && newIndex + 1 < newLines.size() && oldLines[oldIndex] == newLines[newIndex + 1]) {
                entries.push_back({.kind = DiffKind::Insert, .text = newLines[newIndex]});
                ++newIndex;
                continue;
            }
        }

        if (oldIndex < oldLines.size()) {
            entries.push_back({.kind = DiffKind::Delete, .text = oldLines[oldIndex]});
            ++oldIndex;
            continue;
        }

        entries.push_back({.kind = DiffKind::Insert, .text = newLines[newIndex]});
        ++newIndex;
    }

    return entries;
}

std::string formatDiffRange(std::size_t line, std::size_t count) {
    if (count == 0)
        return std::to_string(line > 0 ? line - 1 : 0) + ",0";
    return std::to_string(line) + "," + std::to_string(count);
}

std::string buildUnifiedDiff(std::string_view original, std::string_view updated) {
    const std::vector<std::string> oldLines = splitLines(original);
    const std::vector<std::string> newLines = splitLines(updated);
    const std::vector<DiffEntry> entries = buildLineEditScript(oldLines, newLines);

    constexpr std::size_t kContextLines = 3;
    std::string diff;
    std::size_t index = 0;

    while (index < entries.size()) {
        while (index < entries.size() && entries[index].kind == DiffKind::Context)
            ++index;
        if (index >= entries.size())
            break;

        const std::size_t firstChange = index;
        std::size_t lastChange = index;
        std::size_t contextRun = 0;
        std::size_t cursor = index;

        while (cursor < entries.size()) {
            if (entries[cursor].kind == DiffKind::Context) {
                ++contextRun;
                if (contextRun > kContextLines * 2)
                    break;
            } else {
                lastChange = cursor;
                contextRun = 0;
            }
            ++cursor;
        }

        const std::size_t hunkStart = firstChange > kContextLines ? firstChange - kContextLines : 0;
        const std::size_t hunkEnd = std::min(entries.size(), lastChange + 1 + kContextLines);

        std::size_t oldLine = 1;
        std::size_t newLine = 1;
        for (std::size_t position = 0; position < hunkStart; ++position) {
            if (entries[position].kind != DiffKind::Insert)
                ++oldLine;
            if (entries[position].kind != DiffKind::Delete)
                ++newLine;
        }

        std::size_t oldCount = 0;
        std::size_t newCount = 0;
        for (std::size_t position = hunkStart; position < hunkEnd; ++position) {
            if (entries[position].kind != DiffKind::Insert)
                ++oldCount;
            if (entries[position].kind != DiffKind::Delete)
                ++newCount;
        }

        diff += "@@ -" + formatDiffRange(oldLine, oldCount) + " +" + formatDiffRange(newLine, newCount) + " @@\n";
        for (std::size_t position = hunkStart; position < hunkEnd; ++position) {
            switch (entries[position].kind) {
            case DiffKind::Context:
                diff += " " + entries[position].text + "\n";
                break;
            case DiffKind::Delete:
                diff += "-" + entries[position].text + "\n";
                break;
            case DiffKind::Insert:
                diff += "+" + entries[position].text + "\n";
                break;
            }
        }

        index = hunkEnd;
    }

    return diff;
}

std::string profileField(const char* fieldName) {
    std::string expression = "hdrcalProfile and hdrcalProfile.";
    expression += fieldName;
    expression += " or nil";
    return expression;
}

} // namespace

ConfigFileContent readConfigFileContent(const std::filesystem::path& filePath) {
    std::ifstream input(filePath, std::ios::binary);
    if (!input.is_open())
        return {"", "não foi possível abrir o arquivo de configuração: " + filePath.string()};

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad())
        return {"", "falha de leitura no arquivo de configuração: " + filePath.string()};

    return {buffer.str(), std::nullopt};
}

IntegrationPlan planMonitorRuleIntegration(std::string_view fileContent, std::string_view outputName) {
    IntegrationPlan plan;

    if (const auto outputError = findOutputNameValidationError(outputName); outputError.has_value()) {
        plan.error = "saída inválida para a integração: " + *outputError;
        return plan;
    }

    const std::vector<LuaMonitorCall> calls = findMatchingLuaMonitorCalls(fileContent, outputName);
    if (calls.empty()) {
        plan.error = "nenhuma chamada hl.monitor com output exatamente igual a '" + std::string(outputName) + "' foi encontrada no arquivo";
        return plan;
    }
    if (calls.size() > 1) {
        plan.error = "o arquivo contém " + std::to_string(calls.size()) + " chamadas hl.monitor para a saída '" + std::string(outputName) +
                     "'; deixe apenas uma regra por nome de saída para evitar ambiguidade";
        return plan;
    }

    const LuaMonitorCall& call = calls.front();
    const TableFields fields = findTopLevelFields(fileContent, call);

    if (fields.minimum && fields.maximum && fields.maximumFullFrame) {
        plan.updatedContent = std::string(fileContent);
        plan.alreadyIntegrated = true;
        return plan;
    }

    if (fields.minimum || fields.maximum || fields.maximumFullFrame) {
        plan.error = "a regra de '" + std::string(outputName) +
                     "' já contém parte dos campos de luminância (min_luminance, max_luminance, max_avg_luminance); "
                     "ajuste os três manualmente para evitar chaves duplicadas";
        return plan;
    }

    const std::size_t lineStart = lineStartBefore(fileContent, call.callStart);
    const std::size_t firstNonSpace = fileContent.find_first_not_of(" \t", lineStart);
    const bool addsProfileLocals = fileContent.find("hdrcalProfile") == std::string_view::npos;
    if (addsProfileLocals && firstNonSpace != call.callStart) {
        plan.error = "a chamada hl.monitor de '" + std::string(outputName) +
                     "' não começa em uma linha própria; coloque a chamada no início da linha para que o hdrcal possa "
                     "inserir o bloco de perfil sem quebrar a expressão";
        return plan;
    }

    std::string updated(fileContent);

    const std::size_t closeLineStart = lineStartBefore(fileContent, call.tableClose);
    const std::string_view closePrefix = fileContent.substr(closeLineStart, call.tableClose - closeLineStart);
    const bool closeBraceAlone = std::all_of(closePrefix.begin(), closePrefix.end(), [](char character) { return character == ' ' || character == '\t'; });

    std::size_t lastNonSpace = call.tableClose;
    while (lastNonSpace > call.tableOpen + 1 && std::isspace(static_cast<unsigned char>(updated[lastNonSpace - 1])) != 0)
        --lastNonSpace;

    const bool emptyTable = lastNonSpace == call.tableOpen + 1;
    const bool needsComma = !emptyTable && updated[lastNonSpace - 1] != ',';

    if (closeBraceAlone) {
        const std::string closeIndent(closePrefix);
        const std::string fieldIndent = detectFieldIndent(fileContent, call, closeIndent);
        std::string fields;
        fields += fieldIndent + "min_luminance = " + profileField("min_luminance") + ",\n";
        fields += fieldIndent + "max_luminance = " + profileField("max_luminance") + ",\n";
        fields += fieldIndent + "max_avg_luminance = " + profileField("max_avg_luminance") + ",\n";

        std::size_t insertionPoint = closeLineStart;
        if (needsComma) {
            updated.insert(lastNonSpace, ",");
            ++insertionPoint;
        }
        updated.insert(insertionPoint, fields);
    } else {
        std::string fields;
        if (needsComma)
            fields += ",";
        if (!emptyTable)
            fields += " ";
        fields += "min_luminance = " + profileField("min_luminance");
        fields += ", max_luminance = " + profileField("max_luminance");
        fields += ", max_avg_luminance = " + profileField("max_avg_luminance");
        updated.insert(lastNonSpace, fields);
    }

    if (addsProfileLocals) {
        const std::size_t indentEnd = fileContent.find_first_not_of(" \t", lineStart);
        const std::string indent = indentEnd == std::string_view::npos ? std::string{} : std::string(fileContent.substr(lineStart, indentEnd - lineStart));

        std::string locals;
        locals += indent + "local hdrcalOk, hdrcalModule = pcall(require, \"hdrcal\")\n";
        locals += indent + "local hdrcalProfile = hdrcalOk and hdrcalModule and hdrcalModule.profiles and hdrcalModule.profiles[\"" + std::string(outputName) +
                  "\"] or nil\n\n";
        updated.insert(lineStart, locals);
    }

    plan.updatedContent = std::move(updated);
    plan.diff = buildUnifiedDiff(fileContent, plan.updatedContent);
    return plan;
}

ConfigEditTransaction::ConfigEditTransaction(std::filesystem::path filePath, std::string originalContent, std::string updatedContent)
    : m_filePath(std::move(filePath)), m_originalContent(std::move(originalContent)), m_updatedContent(std::move(updatedContent)) {}

ConfigEditTransaction::~ConfigEditTransaction() {
    if (m_written && !m_committed)
        (void)restoreOriginal();
}

std::filesystem::path ConfigEditTransaction::buildBackupPath() const {
    std::string timestamp = formatUtcTimestamp(std::chrono::system_clock::now());
    std::replace(timestamp.begin(), timestamp.end(), ':', '-');

    const std::filesystem::path base = m_filePath.string() + ".bak.hdrcal." + timestamp;
    std::error_code filesystemError;
    if (!std::filesystem::exists(base, filesystemError))
        return base;

    for (int suffix = 1; suffix < 1000; ++suffix) {
        const std::filesystem::path candidate = base.string() + "." + std::to_string(suffix);
        if (!std::filesystem::exists(candidate, filesystemError))
            return candidate;
    }

    return base.string() + ".duplicate";
}

std::optional<std::string> ConfigEditTransaction::write() {
    if (m_written)
        return "a transação de configuração já foi escrita";
    if (m_committed)
        return "a transação de configuração já foi concluída";
    if (m_updatedContent == m_originalContent)
        return "a configuração já está integrada; nenhuma escrita necessária";

    std::error_code filesystemError;
    if (::access(m_filePath.c_str(), W_OK) != 0)
        return "sem permissão de escrita em " + m_filePath.string() + ": " + std::string(std::strerror(errno));

    const std::filesystem::path parentDirectory = m_filePath.parent_path().empty() ? std::filesystem::path(".") : m_filePath.parent_path();
    if (::access(parentDirectory.c_str(), W_OK) != 0)
        return "sem permissão de escrita no diretório " + parentDirectory.string() + ": " + std::string(std::strerror(errno));

    const std::filesystem::file_status originalStatus = std::filesystem::status(m_filePath, filesystemError);
    if (filesystemError)
        return "não foi possível ler as permissões de " + m_filePath.string() + ": " + filesystemError.message();

    m_originalPermissions = originalStatus.permissions();
    m_hasOriginalPermissions = true;
    m_backupPath = buildBackupPath();

    if (const auto backupError = writeFileAtomically(m_backupPath, m_originalContent); backupError.has_value())
        return "não foi possível criar o backup " + m_backupPath.string() + ": " + *backupError;

    if (m_hasOriginalPermissions)
        std::filesystem::permissions(m_backupPath, m_originalPermissions, std::filesystem::perm_options::replace, filesystemError);

    if (const auto writeError = writeFileAtomically(m_filePath, m_updatedContent); writeError.has_value())
        return "não foi possível escrever " + m_filePath.string() + ": " + *writeError + " (backup em " + m_backupPath.string() + ")";

    if (m_hasOriginalPermissions)
        std::filesystem::permissions(m_filePath, m_originalPermissions, std::filesystem::perm_options::replace, filesystemError);

    m_written = true;
    return std::nullopt;
}

std::optional<std::string> ConfigEditTransaction::commitAfterValidation(const std::function<std::optional<std::string>()>& validation) {
    if (!m_written)
        return "a transação de configuração ainda não foi escrita";

    if (const std::optional<std::string> validationError = validation(); validationError.has_value()) {
        const std::optional<std::string> restoreError = restoreOriginal();
        if (restoreError.has_value())
            return "validação falhou: " + *validationError + "; a restauração também falhou: " + *restoreError + " (backup em " + m_backupPath.string() + ")";
        return "validação falhou: " + *validationError + "; alteração desfeita a partir do backup " + m_backupPath.string();
    }

    m_committed = true;
    return std::nullopt;
}

std::optional<std::string> ConfigEditTransaction::restoreOriginal() {
    if (const auto restoreError = writeFileAtomically(m_filePath, m_originalContent); restoreError.has_value())
        return "não foi possível restaurar " + m_filePath.string() + ": " + *restoreError;

    if (m_hasOriginalPermissions) {
        std::error_code filesystemError;
        std::filesystem::permissions(m_filePath, m_originalPermissions, std::filesystem::perm_options::replace, filesystemError);
    }

    m_written = false;
    return std::nullopt;
}

const std::filesystem::path& ConfigEditTransaction::backupPath() const {
    return m_backupPath;
}

} // namespace hdrcal
