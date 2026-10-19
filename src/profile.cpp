// Implements profile validation, locale-independent Lua rendering and atomic writes.

#include "hdrcal/profile.hpp"

#include "hdrcal/atomic_file.hpp"
#include "hdrcal/number_format.hpp"
#include "hdrcal/pq.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

namespace hdrcal {
namespace {

bool isAsciiDigit(char character) {
    return character >= '0' && character <= '9';
}

bool isPrintableAscii(std::string_view text) {
    return std::all_of(text.begin(), text.end(),
                       [](char character) { return static_cast<unsigned char>(character) >= 0x20 && static_cast<unsigned char>(character) <= 0x7E; });
}

int parseTwoDigits(std::string_view text, std::size_t offset) {
    return (text[offset] - '0') * 10 + (text[offset + 1] - '0');
}

int parseFourDigits(std::string_view text, std::size_t offset) {
    return (text[offset] - '0') * 1000 + (text[offset + 1] - '0') * 100 + (text[offset + 2] - '0') * 10 + (text[offset + 3] - '0');
}

bool isValidUtcTimestamp(std::string_view timestamp) {
    if (timestamp.size() != 20)
        return false;
    if (timestamp[4] != '-' || timestamp[7] != '-' || timestamp[10] != 'T' || timestamp[13] != ':' || timestamp[16] != ':' || timestamp[19] != 'Z')
        return false;

    static constexpr std::size_t kDigitPositions[] = {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18};
    for (const std::size_t position : kDigitPositions) {
        if (!isAsciiDigit(timestamp[position]))
            return false;
    }

    const int year = parseFourDigits(timestamp, 0);
    const int month = parseTwoDigits(timestamp, 5);
    const int day = parseTwoDigits(timestamp, 8);
    const int hour = parseTwoDigits(timestamp, 11);
    const int minute = parseTwoDigits(timestamp, 14);
    const int second = parseTwoDigits(timestamp, 17);

    return year >= 1970 && month >= 1 && month <= 12 && day >= 1 && day <= 31 && hour <= 23 && minute <= 59 && second <= 59;
}

std::string escapeLuaString(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());

    for (const char character : text) {
        switch (character) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20) {
                char escapeBuffer[8];
                std::snprintf(escapeBuffer, sizeof escapeBuffer, "\\%03u", static_cast<unsigned int>(static_cast<unsigned char>(character)));
                escaped += escapeBuffer;
            } else {
                escaped.push_back(character);
            }
            break;
        }
    }

    return escaped;
}

} // namespace

std::optional<std::string> findOutputNameValidationError(std::string_view output) {
    if (output.empty())
        return "o nome da saída não pode ser vazio";
    if (output.size() > 64)
        return "o nome da saída deve ter no máximo 64 caracteres";

    for (const char character : output) {
        const unsigned char value = static_cast<unsigned char>(character);
        const bool isLetter = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
        const bool isDigit = value >= '0' && value <= '9';
        if (!isLetter && !isDigit && character != '-' && character != '_')
            return "o nome da saída contém caractere inválido; use apenas letras, dígitos, '-' e '_'";
    }

    return std::nullopt;
}

std::optional<std::string> findProfileValidationError(const CalibrationProfile& profile) {
    if (const auto outputError = findOutputNameValidationError(profile.output); outputError.has_value())
        return outputError;

    if (!std::isfinite(profile.minimumLuminanceNits))
        return "a luminância mínima deve ser um número finito";
    if (profile.minimumLuminanceNits < 0.0 || profile.minimumLuminanceNits > kPqMaximumDisplayLuminanceNits)
        return "a luminância mínima deve estar entre 0 e 10000 nits";

    if (profile.maximumLuminanceNits < 1 || profile.maximumLuminanceNits > static_cast<int>(kPqMaximumDisplayLuminanceNits))
        return "a luminância máxima deve estar entre 1 e 10000 nits";

    if (profile.maximumFullFrameLuminanceNits < 1 || profile.maximumFullFrameLuminanceNits > static_cast<int>(kPqMaximumDisplayLuminanceNits))
        return "a luminância máxima de frame completo deve estar entre 1 e 10000 nits";

    if (profile.minimumLuminanceNits >= static_cast<double>(profile.maximumLuminanceNits))
        return "a luminância mínima deve ser menor que a luminância máxima";

    if (profile.maximumFullFrameLuminanceNits > profile.maximumLuminanceNits)
        return "a luminância máxima de frame completo não pode exceder a luminância máxima";

    if (!isValidUtcTimestamp(profile.timestampUtc))
        return "o timestamp deve estar no formato UTC ISO 8601 'YYYY-MM-DDTHH:MM:SSZ'";

    if (profile.osdSnapshot.has_value()) {
        const MonitorOsdSnapshot& snapshot = *profile.osdSnapshot;
        if (snapshot.brightness < 0 || snapshot.brightness > 100)
            return "o brilho do OSD deve estar entre 0 e 100";
        if (snapshot.contrast < 0 || snapshot.contrast > 100)
            return "o contraste do OSD deve estar entre 0 e 100";
        if (snapshot.colorPreset.empty())
            return "o preset de cor do OSD não pode ser vazio";
        if (snapshot.colorPreset.size() > 64)
            return "o preset de cor do OSD deve ter no máximo 64 caracteres";
        if (!isPrintableAscii(snapshot.colorPreset))
            return "o preset de cor do OSD deve conter apenas ASCII imprimível";
    }

    return std::nullopt;
}

RenderedLuaTable renderProfileLuaTable(const CalibrationProfile& profile) {
    if (const auto validationError = findProfileValidationError(profile); validationError.has_value())
        return RenderedLuaTable{.content = "", .validationError = validationError};

    std::string content;
    content += "-- Generated by hdrcal. Do not edit by hand.\n";
    content += "return {\n";
    content += "    version = 1,\n";
    content += "    profiles = {\n";
    content += "        [\"";
    content += escapeLuaString(profile.output);
    content += "\"] = {\n";
    content += "            min_luminance = ";
    content += formatLocaleIndependentDouble(profile.minimumLuminanceNits);
    content += ",\n";
    content += "            max_luminance = ";
    content += std::to_string(profile.maximumLuminanceNits);
    content += ",\n";
    content += "            max_avg_luminance = ";
    content += std::to_string(profile.maximumFullFrameLuminanceNits);
    content += ",\n";
    content += "            calibrated_at = \"";
    content += escapeLuaString(profile.timestampUtc);
    content += "\",\n";

    if (profile.osdSnapshot.has_value()) {
        const MonitorOsdSnapshot& snapshot = *profile.osdSnapshot;
        content += "            osd = {\n";
        content += "                brightness = ";
        content += std::to_string(snapshot.brightness);
        content += ",\n";
        content += "                contrast = ";
        content += std::to_string(snapshot.contrast);
        content += ",\n";
        content += "                color_preset = \"";
        content += escapeLuaString(snapshot.colorPreset);
        content += "\",\n";
        content += "            },\n";
    }

    content += "        },\n";
    content += "    },\n";
    content += "}\n";

    return RenderedLuaTable{.content = std::move(content), .validationError = std::nullopt};
}

std::optional<std::string> saveProfileToPath(const CalibrationProfile& profile, const std::filesystem::path& destinationPath) {
    const RenderedLuaTable rendered = renderProfileLuaTable(profile);
    if (rendered.validationError.has_value())
        return rendered.validationError;

    return writeFileAtomically(destinationPath, rendered.content);
}

std::optional<std::filesystem::path> readProfilePathEnvironmentOverride() {
    const char* environmentPath = std::getenv(std::string(kProfilePathEnvironmentVariable).c_str());
    if (environmentPath == nullptr || *environmentPath == '\0')
        return std::nullopt;
    return std::filesystem::path(environmentPath);
}

std::filesystem::path resolveProfilePath(const std::optional<std::filesystem::path>& commandLinePath,
                                         const std::optional<std::filesystem::path>& environmentPath, const std::filesystem::path& defaultPath) {
    if (commandLinePath.has_value() && !commandLinePath->empty())
        return *commandLinePath;
    if (environmentPath.has_value() && !environmentPath->empty())
        return *environmentPath;
    return defaultPath;
}

std::string formatUtcTimestamp(std::chrono::system_clock::time_point timestamp) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(timestamp);
    std::tm utcTime{};
    if (::gmtime_r(&seconds, &utcTime) == nullptr)
        return "";

    char buffer[80];
    std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02dT%02d:%02d:%02dZ", utcTime.tm_year + 1900, utcTime.tm_mon + 1, utcTime.tm_mday, utcTime.tm_hour,
                  utcTime.tm_min, utcTime.tm_sec);
    return buffer;
}

} // namespace hdrcal
