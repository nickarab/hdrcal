// 16-bit PQ pattern generation for the three visual calibration phases.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace hdrcal {

enum class PatternPhase {
    MinimumLuminance,
    MaximumLuminance,
    MaximumFullFrameLuminance,
};

// Stable lowercase identifier used in temporary file names ("minimum",
// "maximum", "full-frame").
[[nodiscard]] std::string patternPhaseName(PatternPhase phase);

struct PatternSpec {
    PatternPhase phase = PatternPhase::MinimumLuminance;
    std::uint16_t tenBitCode = 0;
    int width = 2560;
    int height = 1440;
};

// Exact mapping from a 10-bit PQ code in [0, 1023] to a full-range 16-bit PNG
// sample in [0, 65535]. Values above 1023 are clamped.
[[nodiscard]] std::uint16_t computeSixteenBitSampleFromTenBitCode(std::uint16_t tenBitCode);

// Generates a PNG with colour type RGB, 16 bits per component, and a cICP
// chunk (BT.2020 primaries, PQ transfer, RGB matrix, full range). The image is
// written atomically. Minimum and maximum luminance phases use a centred patch
// on black; the maximum phase uses a checkerboard of `code` and `code - 16` to
// make clipping visible. The full-frame phase uses a uniform field.
[[nodiscard]] std::optional<std::string> writePqPatternPng(const PatternSpec& spec, const std::filesystem::path& destinationPath);

} // namespace hdrcal
