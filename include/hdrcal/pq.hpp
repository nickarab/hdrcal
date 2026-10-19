// PQ (ST 2084) transfer-function conversions used by calibration patterns and profile math.

#pragma once

#include <cstdint>

namespace hdrcal {

// ST 2084 (PQ) constants. The display luminance is expressed in nits.
inline constexpr double kPqMaximumDisplayLuminanceNits = 10000.0;
inline constexpr double kPqM1 = 0.1593017578125;
inline constexpr double kPqM2 = 78.84375;
inline constexpr double kPqC1 = 0.8359375;
inline constexpr double kPqC2 = 18.8515625;
inline constexpr double kPqC3 = 18.6875;

// A 10-bit code value is an integer in [0, 1023].
inline constexpr std::uint16_t kTenBitMaximumSample = 1023;

// Converts a normalized PQ code in [0, 1] to absolute display luminance in
// nits. Values outside [0, 1] are clamped. NaN is propagated as NaN because it
// indicates a caller bug, not a valid boundary value.
[[nodiscard]] double computeLuminanceFromPqCode(double pqCode);

// Converts absolute display luminance in nits to a normalized PQ code in
// [0, 1]. Values outside [0, 10000] are clamped. NaN is propagated as NaN.
[[nodiscard]] double computePqCodeFromLuminance(double luminanceNits);

// Converts a normalized PQ code in [0, 1] to the nearest 10-bit sample in
// [0, 1023]. Values outside [0, 1] are clamped.
[[nodiscard]] std::uint16_t computeTenBitSampleFromPqCode(double pqCode);

// Converts a 10-bit sample to its normalized PQ code. Samples above 1023 are
// clamped so that corrupt input cannot produce a code above 1.
[[nodiscard]] double computePqCodeFromTenBitSample(std::uint16_t tenBitSample);

} // namespace hdrcal
