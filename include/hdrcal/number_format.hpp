// Locale-independent numeric formatting shared by Lua output and Hyprland input.

#pragma once

#include <charconv>
#include <string>
#include <system_error>

namespace hdrcal {

// Formats a double with '.' as decimal separator and the shortest round-trip
// representation. Used for Lua and Hyprland input, both of which must not
// depend on the process locale.
[[nodiscard]] inline std::string formatLocaleIndependentDouble(double value) {
    // Canonicalize negative zero: the shortest round-trip form would be "-0",
    // Lua preserves the sign of zero, and a generated profile should not change
    // bytes merely because a measured value arrived as -0.0. Tested by
    // testFormatLocaleIndependentDouble_NegativeZero_EmitsCanonicalZero.
    if (value == 0.0)
        value = 0.0;

    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::general);
    if (result.ec != std::errc{})
        return "0";
    return std::string(buffer, result.ptr);
}

} // namespace hdrcal
