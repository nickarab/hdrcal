// Implements the PQ (ST 2084) conversions declared in pq.hpp.

#include "hdrcal/pq.hpp"

#include <algorithm>
#include <cmath>

namespace hdrcal {

double computeLuminanceFromPqCode(double pqCode) {
    if (std::isnan(pqCode))
        return pqCode;

    const double code = std::clamp(pqCode, 0.0, 1.0);
    if (code == 0.0)
        return 0.0;

    // Inverse of the ST 2084 EOTF. The denominator is strictly positive for
    // every code in [0, 1].
    const double powered = std::pow(code, 1.0 / kPqM2);
    const double numerator = std::max(powered - kPqC1, 0.0);
    const double denominator = kPqC2 - kPqC3 * powered;

    return kPqMaximumDisplayLuminanceNits * std::pow(numerator / denominator, 1.0 / kPqM1);
}

double computePqCodeFromLuminance(double luminanceNits) {
    if (std::isnan(luminanceNits))
        return luminanceNits;

    const double normalized = std::clamp(luminanceNits, 0.0, kPqMaximumDisplayLuminanceNits) / kPqMaximumDisplayLuminanceNits;
    if (normalized == 0.0)
        return 0.0;

    const double powered = std::pow(normalized, kPqM1);
    return std::pow((kPqC1 + kPqC2 * powered) / (1.0 + kPqC3 * powered), kPqM2);
}

std::uint16_t computeTenBitSampleFromPqCode(double pqCode) {
    if (std::isnan(pqCode))
        return 0;

    const double clamped = std::clamp(pqCode, 0.0, 1.0);
    return static_cast<std::uint16_t>(std::lround(clamped * static_cast<double>(kTenBitMaximumSample)));
}

double computePqCodeFromTenBitSample(std::uint16_t tenBitSample) {
    const std::uint16_t clamped = std::min(tenBitSample, kTenBitMaximumSample);
    return static_cast<double>(clamped) / static_cast<double>(kTenBitMaximumSample);
}

} // namespace hdrcal
