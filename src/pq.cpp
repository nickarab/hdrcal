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
    const double powered = std::pow(code, 1.0 / kPqM2SignalExponent);
    const double numerator = std::max(powered - kPqC1RationalOffset, 0.0);
    const double denominator = kPqC2RationalNumeratorScale - kPqC3RationalDenominatorScale * powered;

    return kPqMaximumDisplayLuminanceNits * std::pow(numerator / denominator, 1.0 / kPqM1LuminanceExponent);
}

double computePqCodeFromLuminance(double luminanceNits) {
    if (std::isnan(luminanceNits))
        return luminanceNits;

    const double normalized = std::clamp(luminanceNits, 0.0, kPqMaximumDisplayLuminanceNits) / kPqMaximumDisplayLuminanceNits;
    if (normalized == 0.0)
        return 0.0;

    const double powered = std::pow(normalized, kPqM1LuminanceExponent);
    return std::pow((kPqC1RationalOffset + kPqC2RationalNumeratorScale * powered) / (1.0 + kPqC3RationalDenominatorScale * powered), kPqM2SignalExponent);
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
