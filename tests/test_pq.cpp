// Unit tests for the PQ conversions and the permanent anchor table.

#include "hdrcal/pq.hpp"
#include "test_framework.hpp"

#include <cstdint>

namespace {

struct PqAnchor {
    double pqCode;
    std::uint16_t tenBitSample;
    double luminanceNits;
};

// Permanent regression table from section 8.1 of the project instructions.
// The values are rounded, so the tests use tolerances appropriate to each
// representation.
constexpr PqAnchor kKnownAnchors[] = {
    {.pqCode = 0.0151, .tenBitSample = 15, .luminanceNits = 0.005},  {.pqCode = 0.0215, .tenBitSample = 22, .luminanceNits = 0.01},
    {.pqCode = 0.0302, .tenBitSample = 31, .luminanceNits = 0.02},   {.pqCode = 0.0461, .tenBitSample = 47, .luminanceNits = 0.05},
    {.pqCode = 0.5086, .tenBitSample = 520, .luminanceNits = 100.0}, {.pqCode = 0.5791, .tenBitSample = 592, .luminanceNits = 200.0},
    {.pqCode = 0.6383, .tenBitSample = 653, .luminanceNits = 350.0}, {.pqCode = 0.6963, .tenBitSample = 712, .luminanceNits = 600.0},
    {.pqCode = 0.7403, .tenBitSample = 757, .luminanceNits = 900.0}, {.pqCode = 0.7525, .tenBitSample = 770, .luminanceNits = 1000.0},
};

} // namespace

HDRCAL_TEST(testComputeLuminanceFromPqCode_KnownAnchors_MatchesStd2084) {
    for (const PqAnchor& anchor : kKnownAnchors)
        HDRCAL_REQUIRE_RELATIVE_NEAR(hdrcal::computeLuminanceFromPqCode(anchor.pqCode), anchor.luminanceNits, 0.02);
}

HDRCAL_TEST(testComputeLuminanceFromTenBitAnchors_KnownTable_MatchesStd2084) {
    for (const PqAnchor& anchor : kKnownAnchors) {
        const double pqCode = hdrcal::computePqCodeFromTenBitSample(anchor.tenBitSample);
        // Sample 15 is the only rounded entry that differs by more than one
        // percent from the nominal luminance in the table (0.00474 vs 0.005).
        const double tolerance = anchor.tenBitSample == 15 ? 0.06 : 0.02;
        HDRCAL_REQUIRE_RELATIVE_NEAR(hdrcal::computeLuminanceFromPqCode(pqCode), anchor.luminanceNits, tolerance);
    }
}

HDRCAL_TEST(testComputePqCodeFromLuminance_KnownAnchors_MatchesStd2084) {
    for (const PqAnchor& anchor : kKnownAnchors)
        HDRCAL_REQUIRE_NEAR(hdrcal::computePqCodeFromLuminance(anchor.luminanceNits), anchor.pqCode, 0.0015);
}

HDRCAL_TEST(testComputeLuminanceFromPqCode_Boundaries_ClampsToValidRange) {
    HDRCAL_REQUIRE_EQ(hdrcal::computeLuminanceFromPqCode(-1.0), 0.0);
    HDRCAL_REQUIRE_EQ(hdrcal::computeLuminanceFromPqCode(0.0), 0.0);
    HDRCAL_REQUIRE_EQ(hdrcal::computeLuminanceFromPqCode(1.0), 10000.0);
    HDRCAL_REQUIRE_EQ(hdrcal::computeLuminanceFromPqCode(2.0), 10000.0);
}

HDRCAL_TEST(testComputePqCodeFromLuminance_Boundaries_ClampsToValidRange) {
    HDRCAL_REQUIRE_EQ(hdrcal::computePqCodeFromLuminance(-10.0), 0.0);
    HDRCAL_REQUIRE_EQ(hdrcal::computePqCodeFromLuminance(0.0), 0.0);
    HDRCAL_REQUIRE_EQ(hdrcal::computePqCodeFromLuminance(10000.0), 1.0);
    HDRCAL_REQUIRE_EQ(hdrcal::computePqCodeFromLuminance(20000.0), 1.0);
}

HDRCAL_TEST(testPqCodeAndLuminance_RoundTrip_StaysWithinFloatingPointPrecision) {
    constexpr double kCodesToCheck[] = {0.0, 0.0001, 0.01, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0};

    for (const double pqCode : kCodesToCheck) {
        const double luminance = hdrcal::computeLuminanceFromPqCode(pqCode);
        HDRCAL_REQUIRE_NEAR(hdrcal::computePqCodeFromLuminance(luminance), pqCode, 1e-9);
    }
}

HDRCAL_TEST(testTenBitSampleConversion_MapsEndpointsAndClamps) {
    HDRCAL_REQUIRE_EQ(hdrcal::computeTenBitSampleFromPqCode(-0.5), std::uint16_t{0});
    HDRCAL_REQUIRE_EQ(hdrcal::computeTenBitSampleFromPqCode(0.0), std::uint16_t{0});
    HDRCAL_REQUIRE_EQ(hdrcal::computeTenBitSampleFromPqCode(1.0), std::uint16_t{1023});
    HDRCAL_REQUIRE_EQ(hdrcal::computeTenBitSampleFromPqCode(1.5), std::uint16_t{1023});
    HDRCAL_REQUIRE_EQ(hdrcal::computeTenBitSampleFromPqCode(653.0 / 1023.0), std::uint16_t{653});
    HDRCAL_REQUIRE_EQ(hdrcal::computeTenBitSampleFromPqCode(712.0 / 1023.0), std::uint16_t{712});
}

HDRCAL_TEST(testPqCodeFromTenBitSample_ClampsAboveMaximum) {
    HDRCAL_REQUIRE_EQ(hdrcal::computePqCodeFromTenBitSample(0), 0.0);
    HDRCAL_REQUIRE_EQ(hdrcal::computePqCodeFromTenBitSample(1023), 1.0);
    HDRCAL_REQUIRE_EQ(hdrcal::computePqCodeFromTenBitSample(65535), 1.0);
}
