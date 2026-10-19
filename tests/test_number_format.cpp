// Unit tests for locale-independent number formatting and profile path priority.

#include "hdrcal/number_format.hpp"
#include "hdrcal/profile.hpp"
#include "test_framework.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

HDRCAL_TEST(testFormatLocaleIndependentDouble_NegativeZero_EmitsCanonicalZero) {
    HDRCAL_REQUIRE_EQ(hdrcal::formatLocaleIndependentDouble(-0.0), std::string("0"));
    HDRCAL_REQUIRE_EQ(hdrcal::formatLocaleIndependentDouble(0.0), std::string("0"));
}

HDRCAL_TEST(testFormatLocaleIndependentDouble_DecimalPointAndRoundTrip) {
    HDRCAL_REQUIRE_EQ(hdrcal::formatLocaleIndependentDouble(0.02), std::string("0.02"));
    HDRCAL_REQUIRE_EQ(hdrcal::formatLocaleIndependentDouble(600.5), std::string("600.5"));
    HDRCAL_REQUIRE(hdrcal::formatLocaleIndependentDouble(0.02).find(',') == std::string::npos);
}

HDRCAL_TEST(testResolveProfilePath_CommandLineWinsOverEnvironmentAndDefault) {
    const std::filesystem::path resolved = hdrcal::resolveProfilePath(std::filesystem::path("/tmp/from-command-line"),
                                                                      std::filesystem::path("/tmp/from-environment"), std::filesystem::path("/tmp/default"));
    HDRCAL_REQUIRE_EQ(resolved, std::filesystem::path("/tmp/from-command-line"));
}

HDRCAL_TEST(testResolveProfilePath_EnvironmentWinsOverDefault) {
    const std::filesystem::path resolved =
        hdrcal::resolveProfilePath(std::nullopt, std::filesystem::path("/tmp/from-environment"), std::filesystem::path("/tmp/default"));
    HDRCAL_REQUIRE_EQ(resolved, std::filesystem::path("/tmp/from-environment"));
}

HDRCAL_TEST(testResolveProfilePath_DefaultUsedWithoutOverrides) {
    const std::filesystem::path resolved = hdrcal::resolveProfilePath(std::nullopt, std::nullopt, std::filesystem::path("/tmp/default"));
    HDRCAL_REQUIRE_EQ(resolved, std::filesystem::path("/tmp/default"));
}

HDRCAL_TEST(testReadProfilePathEnvironmentOverride_ReadsNonEmptyVariable) {
    HDRCAL_REQUIRE_EQ(hdrcal::kProfilePathEnvironmentVariable, std::string("HDRICAL_PROFILE_PATH"));

    (void)::setenv("HDRICAL_PROFILE_PATH", "/tmp/profile-from-env.lua", 1);
    const std::optional<std::filesystem::path> fromEnvironment = hdrcal::readProfilePathEnvironmentOverride();
    (void)::unsetenv("HDRICAL_PROFILE_PATH");

    HDRCAL_REQUIRE(fromEnvironment.has_value());
    HDRCAL_REQUIRE_EQ(*fromEnvironment, std::filesystem::path("/tmp/profile-from-env.lua"));
}

HDRCAL_TEST(testReadProfilePathEnvironmentOverride_EmptyVariableIsIgnored) {
    (void)::setenv("HDRICAL_PROFILE_PATH", "", 1);
    const std::optional<std::filesystem::path> fromEnvironment = hdrcal::readProfilePathEnvironmentOverride();
    (void)::unsetenv("HDRICAL_PROFILE_PATH");

    HDRCAL_REQUIRE(!fromEnvironment.has_value());
}
