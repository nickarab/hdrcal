// Implements the minimal JSON parser declared in json.hpp.

#include "hdrcal/json.hpp"

#include <charconv>
#include <cstdint>
#include <system_error>
#include <utility>

namespace hdrcal {
namespace {

void appendUtf8(std::string& destination, std::uint32_t codePoint) {
    if (codePoint <= 0x7F) {
        destination.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FF) {
        destination.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        destination.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint <= 0xFFFF) {
        destination.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        destination.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        destination.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        destination.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        destination.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        destination.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        destination.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

class Parser {
public:
    explicit Parser(std::string_view text) : m_text(text) {}

    JsonParseResult parse() {
        skipWhitespace();
        JsonValue value;
        if (!parseValue(value))
            return {std::nullopt, m_error};

        skipWhitespace();
        if (m_position != m_text.size()) {
            setError("unexpected trailing characters");
            return {std::nullopt, m_error};
        }

        return {std::move(value), {}};
    }

private:
    bool parseValue(JsonValue& destination) {
        if (m_position >= m_text.size()) {
            setError("unexpected end of input");
            return false;
        }

        switch (m_text[m_position]) {
        case '{':
            return parseObject(destination);
        case '[':
            return parseArray(destination);
        case '"': {
            std::string value;
            if (!parseString(value))
                return false;
            destination = JsonValue(std::move(value));
            return true;
        }
        case 't':
            if (!consumeLiteral("true"))
                return false;
            destination = JsonValue(true);
            return true;
        case 'f':
            if (!consumeLiteral("false"))
                return false;
            destination = JsonValue(false);
            return true;
        case 'n':
            if (!consumeLiteral("null"))
                return false;
            destination = JsonValue(nullptr);
            return true;
        default:
            return parseNumber(destination);
        }
    }

    bool parseObject(JsonValue& destination) {
        ++m_position; // '{'
        JsonValue::Object object;
        skipWhitespace();

        if (consumeIf('}')) {
            destination = JsonValue(std::move(object));
            return true;
        }

        while (true) {
            skipWhitespace();
            if (m_position >= m_text.size() || m_text[m_position] != '"') {
                setError("expected object key string");
                return false;
            }

            std::string key;
            if (!parseString(key))
                return false;

            skipWhitespace();
            if (!consumeIf(':')) {
                setError("expected ':' after object key");
                return false;
            }

            skipWhitespace();
            JsonValue value;
            if (!parseValue(value))
                return false;

            if (!object.emplace(std::move(key), std::move(value)).second) {
                setError("duplicate object key");
                return false;
            }

            skipWhitespace();
            if (consumeIf('}')) {
                destination = JsonValue(std::move(object));
                return true;
            }
            if (!consumeIf(',')) {
                setError("expected ',' or '}' in object");
                return false;
            }
        }
    }

    bool parseArray(JsonValue& destination) {
        ++m_position; // '['
        JsonValue::Array array;
        skipWhitespace();

        if (consumeIf(']')) {
            destination = JsonValue(std::move(array));
            return true;
        }

        while (true) {
            skipWhitespace();
            JsonValue value;
            if (!parseValue(value))
                return false;
            array.push_back(std::move(value));

            skipWhitespace();
            if (consumeIf(']')) {
                destination = JsonValue(std::move(array));
                return true;
            }
            if (!consumeIf(',')) {
                setError("expected ',' or ']' in array");
                return false;
            }
        }
    }

    bool parseString(std::string& destination) {
        ++m_position; // opening quote
        destination.clear();

        while (m_position < m_text.size()) {
            const char character = m_text[m_position++];
            if (character == '"')
                return true;

            if (static_cast<unsigned char>(character) < 0x20) {
                setError("unescaped control character in string");
                return false;
            }

            if (character != '\\') {
                destination.push_back(character);
                continue;
            }

            if (m_position >= m_text.size()) {
                setError("unterminated escape sequence");
                return false;
            }

            const char escape = m_text[m_position++];
            switch (escape) {
            case '"':
                destination.push_back('"');
                break;
            case '\\':
                destination.push_back('\\');
                break;
            case '/':
                destination.push_back('/');
                break;
            case 'b':
                destination.push_back('\b');
                break;
            case 'f':
                destination.push_back('\f');
                break;
            case 'n':
                destination.push_back('\n');
                break;
            case 'r':
                destination.push_back('\r');
                break;
            case 't':
                destination.push_back('\t');
                break;
            case 'u': {
                std::uint32_t codePoint = 0;
                if (!parseHexCodeUnit(codePoint))
                    return false;

                // Combine UTF-16 surrogate pairs into one code point.
                if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
                    if (m_position + 1 >= m_text.size() || m_text[m_position] != '\\' || m_text[m_position + 1] != 'u') {
                        setError("unpaired high surrogate in string");
                        return false;
                    }
                    m_position += 2;

                    std::uint32_t lowSurrogate = 0;
                    if (!parseHexCodeUnit(lowSurrogate))
                        return false;
                    if (lowSurrogate < 0xDC00 || lowSurrogate > 0xDFFF) {
                        setError("invalid low surrogate in string");
                        return false;
                    }

                    codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (lowSurrogate - 0xDC00);
                } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
                    setError("unpaired low surrogate in string");
                    return false;
                }

                appendUtf8(destination, codePoint);
                break;
            }
            default:
                setError("invalid escape sequence in string");
                return false;
            }
        }

        setError("unterminated string");
        return false;
    }

    bool parseHexCodeUnit(std::uint32_t& destination) {
        if (m_position + 4 > m_text.size()) {
            setError("truncated \\u escape in string");
            return false;
        }

        std::uint32_t value = 0;
        for (int digitIndex = 0; digitIndex < 4; ++digitIndex) {
            const char digit = m_text[m_position++];
            value <<= 4;

            if (digit >= '0' && digit <= '9')
                value |= static_cast<std::uint32_t>(digit - '0');
            else if (digit >= 'a' && digit <= 'f')
                value |= static_cast<std::uint32_t>(digit - 'a' + 10);
            else if (digit >= 'A' && digit <= 'F')
                value |= static_cast<std::uint32_t>(digit - 'A' + 10);
            else {
                setError("invalid hexadecimal digit in \\u escape");
                return false;
            }
        }

        destination = value;
        return true;
    }

    bool parseNumber(JsonValue& destination) {
        const std::size_t start = m_position;
        if (m_text[m_position] == '-')
            ++m_position;

        if (m_position >= m_text.size()) {
            setError("truncated number");
            return false;
        }

        if (m_text[m_position] == '0') {
            ++m_position;
        } else if (m_text[m_position] >= '1' && m_text[m_position] <= '9') {
            while (m_position < m_text.size() && m_text[m_position] >= '0' && m_text[m_position] <= '9')
                ++m_position;
        } else {
            setError("unexpected character");
            return false;
        }

        if (m_position < m_text.size() && m_text[m_position] == '.') {
            ++m_position;
            if (m_position >= m_text.size() || m_text[m_position] < '0' || m_text[m_position] > '9') {
                setError("expected digit after decimal point");
                return false;
            }
            while (m_position < m_text.size() && m_text[m_position] >= '0' && m_text[m_position] <= '9')
                ++m_position;
        }

        if (m_position < m_text.size() && (m_text[m_position] == 'e' || m_text[m_position] == 'E')) {
            ++m_position;
            if (m_position < m_text.size() && (m_text[m_position] == '+' || m_text[m_position] == '-'))
                ++m_position;
            if (m_position >= m_text.size() || m_text[m_position] < '0' || m_text[m_position] > '9') {
                setError("expected digit in exponent");
                return false;
            }
            while (m_position < m_text.size() && m_text[m_position] >= '0' && m_text[m_position] <= '9')
                ++m_position;
        }

        double value = 0.0;
        const char* begin = m_text.data() + start;
        const char* end = m_text.data() + m_position;
        const auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{} || result.ptr != end) {
            setError("invalid number");
            return false;
        }

        destination = JsonValue(value);
        return true;
    }

    bool consumeLiteral(std::string_view literal) {
        if (m_text.substr(m_position, literal.size()) != literal) {
            setError("unexpected literal");
            return false;
        }
        m_position += literal.size();
        return true;
    }

    bool consumeIf(char expected) {
        if (m_position < m_text.size() && m_text[m_position] == expected) {
            ++m_position;
            return true;
        }
        return false;
    }

    void skipWhitespace() {
        while (m_position < m_text.size()) {
            const char character = m_text[m_position];
            if (character != ' ' && character != '\t' && character != '\n' && character != '\r')
                return;
            ++m_position;
        }
    }

    void setError(std::string message) {
        m_error = std::move(message);
        m_error += " at byte ";
        m_error += std::to_string(m_position);
    }

    std::string_view m_text;
    std::size_t m_position = 0;
    std::string m_error;
};

} // namespace

JsonValue::JsonValue() : m_storage(nullptr) {}
JsonValue::JsonValue(std::nullptr_t) : m_storage(nullptr) {}
JsonValue::JsonValue(bool value) : m_storage(value) {}
JsonValue::JsonValue(double value) : m_storage(value) {}
JsonValue::JsonValue(std::string value) : m_storage(std::move(value)) {}
JsonValue::JsonValue(Array value) : m_storage(std::move(value)) {}
JsonValue::JsonValue(Object value) : m_storage(std::move(value)) {}

const bool* JsonValue::asBoolean() const {
    return std::get_if<bool>(&m_storage);
}

const double* JsonValue::asNumber() const {
    return std::get_if<double>(&m_storage);
}

const std::string* JsonValue::asString() const {
    return std::get_if<std::string>(&m_storage);
}

const JsonValue::Array* JsonValue::asArray() const {
    return std::get_if<Array>(&m_storage);
}

const JsonValue::Object* JsonValue::asObject() const {
    return std::get_if<Object>(&m_storage);
}

const JsonValue* JsonValue::find(std::string_view key) const {
    const auto* object = asObject();
    if (!object)
        return nullptr;

    const auto iterator = object->find(key);
    return iterator == object->end() ? nullptr : &iterator->second;
}

JsonParseResult parseJson(std::string_view text) {
    return Parser(text).parse();
}

} // namespace hdrcal
