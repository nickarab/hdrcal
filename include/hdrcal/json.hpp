// Minimal JSON parser used to read the output of hyprctl monitors -j.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hdrcal {

// Minimal JSON value used to read `hyprctl monitors -j`. It deliberately
// supports only the data model needed by this project, without external
// dependencies.
class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue, std::less<>>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    JsonValue();
    explicit JsonValue(std::nullptr_t);
    explicit JsonValue(bool value);
    explicit JsonValue(double value);
    explicit JsonValue(std::string value);
    explicit JsonValue(Array value);
    explicit JsonValue(Object value);

    [[nodiscard]] const bool* asBoolean() const;
    [[nodiscard]] const double* asNumber() const;
    [[nodiscard]] const std::string* asString() const;
    [[nodiscard]] const Array* asArray() const;
    [[nodiscard]] const Object* asObject() const;

    // Looks up a member in an object. Returns nullptr for non-objects or
    // missing keys.
    [[nodiscard]] const JsonValue* find(std::string_view key) const;

private:
    Storage m_storage;
};

struct JsonParseResult {
    std::optional<JsonValue> value;
    std::string error;
};

// Parses a complete JSON document. The error string contains the byte offset
// of the first syntax error. No trailing characters are accepted.
[[nodiscard]] JsonParseResult parseJson(std::string_view text);

} // namespace hdrcal
