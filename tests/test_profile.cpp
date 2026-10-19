// Unit tests for profile validation, Lua rendering and atomic persistence.

#include "hdrcal/profile.hpp"
#include "test_framework.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace {

hdrcal::CalibrationProfile makeValidProfile() {
    hdrcal::CalibrationProfile profile;
    profile.output = "DP-2";
    profile.minimumLuminanceNits = 0.02;
    profile.maximumLuminanceNits = 600;
    profile.maximumFullFrameLuminanceNits = 350;
    profile.timestampUtc = "2026-10-19T21:00:00Z";
    return profile;
}

} // namespace

HDRCAL_TEST(testFindProfileValidationError_ValidProfile_ReturnsNoError) {
    HDRCAL_REQUIRE(!hdrcal::findProfileValidationError(makeValidProfile()).has_value());
}

HDRCAL_TEST(testFindProfileValidationError_RejectsMinimumAtOrAboveMaximum) {
    hdrcal::CalibrationProfile profile = makeValidProfile();
    profile.minimumLuminanceNits = 600.0;
    HDRCAL_REQUIRE(hdrcal::findProfileValidationError(profile).has_value());
}

HDRCAL_TEST(testFindProfileValidationError_RejectsAverageAboveMaximum) {
    hdrcal::CalibrationProfile profile = makeValidProfile();
    profile.maximumFullFrameLuminanceNits = 601;
    HDRCAL_REQUIRE(hdrcal::findProfileValidationError(profile).has_value());
}

HDRCAL_TEST(testFindProfileValidationError_RejectsInvalidOutputName) {
    hdrcal::CalibrationProfile profile = makeValidProfile();
    profile.output = "DP-2\"injected";
    HDRCAL_REQUIRE(hdrcal::findProfileValidationError(profile).has_value());
}

HDRCAL_TEST(testFindProfileValidationError_RejectsNonFiniteMinimum) {
    hdrcal::CalibrationProfile profile = makeValidProfile();
    profile.minimumLuminanceNits = std::numeric_limits<double>::quiet_NaN();
    HDRCAL_REQUIRE(hdrcal::findProfileValidationError(profile).has_value());
}

HDRCAL_TEST(testFindProfileValidationError_RejectsInvalidTimestamp) {
    hdrcal::CalibrationProfile profile = makeValidProfile();
    profile.timestampUtc = "2026-10-19 21:00:00";
    HDRCAL_REQUIRE(hdrcal::findProfileValidationError(profile).has_value());
}

HDRCAL_TEST(testFindProfileValidationError_RejectsInvalidOsdValues) {
    hdrcal::CalibrationProfile profile = makeValidProfile();
    profile.osdSnapshot = hdrcal::MonitorOsdSnapshot{.brightness = 101, .contrast = 50, .colorPreset = "User"};
    HDRCAL_REQUIRE(hdrcal::findProfileValidationError(profile).has_value());
}

HDRCAL_TEST(testRenderProfileLuaTable_ValidProfile_ContainsLocaleIndependentNumbers) {
    const hdrcal::RenderedLuaTable rendered = hdrcal::renderProfileLuaTable(makeValidProfile());
    HDRCAL_REQUIRE(!rendered.validationError.has_value());

    HDRCAL_REQUIRE(rendered.content.find("[\"DP-2\"] = {") != std::string::npos);
    HDRCAL_REQUIRE(rendered.content.find("min_luminance = 0.02,") != std::string::npos);
    HDRCAL_REQUIRE(rendered.content.find("max_luminance = 600,") != std::string::npos);
    HDRCAL_REQUIRE(rendered.content.find("max_avg_luminance = 350,") != std::string::npos);
    HDRCAL_REQUIRE(rendered.content.find("calibrated_at = \"2026-10-19T21:00:00Z\",") != std::string::npos);
    HDRCAL_REQUIRE(rendered.content.find("min_luminance = 0,02") == std::string::npos);
}

HDRCAL_TEST(testRenderProfileLuaTable_IncludesOsdSnapshotAndEscapesStrings) {
    hdrcal::CalibrationProfile profile = makeValidProfile();
    profile.osdSnapshot = hdrcal::MonitorOsdSnapshot{.brightness = 80, .contrast = 55, .colorPreset = "User \"A\"\\B"};

    const hdrcal::RenderedLuaTable rendered = hdrcal::renderProfileLuaTable(profile);
    HDRCAL_REQUIRE(!rendered.validationError.has_value());
    HDRCAL_REQUIRE(rendered.content.find("brightness = 80,") != std::string::npos);
    HDRCAL_REQUIRE(rendered.content.find("contrast = 55,") != std::string::npos);
    HDRCAL_REQUIRE(rendered.content.find("color_preset = \"User \\\"A\\\"\\\\B\",") != std::string::npos);
}

HDRCAL_TEST(testRenderProfileLuaTable_InvalidProfile_ReturnsValidationError) {
    hdrcal::CalibrationProfile profile = makeValidProfile();
    profile.maximumFullFrameLuminanceNits = 0;

    const hdrcal::RenderedLuaTable rendered = hdrcal::renderProfileLuaTable(profile);
    HDRCAL_REQUIRE(rendered.validationError.has_value());
    HDRCAL_REQUIRE(rendered.content.empty());
}

HDRCAL_TEST(testSaveProfileToPath_WritesFileAndLeavesNoTemporaryArtifact) {
    const hdrcal::test::TemporaryDirectory temporaryDirectory("hdrcal-profile-save");
    const std::filesystem::path destination = temporaryDirectory.path() / "nested" / "hdrcal.lua";

    HDRCAL_REQUIRE(!hdrcal::saveProfileToPath(makeValidProfile(), destination).has_value());

    std::ifstream input(destination);
    HDRCAL_REQUIRE(input.is_open());
    const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    HDRCAL_REQUIRE(content.find("[\"DP-2\"] = {") != std::string::npos);

    std::size_t fileCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(destination.parent_path())) {
        if (entry.is_regular_file())
            ++fileCount;
    }
    HDRCAL_REQUIRE_EQ(fileCount, std::size_t{1});
}

HDRCAL_TEST(testSaveProfileToPath_OverwritesPreviousProfileAtomically) {
    const hdrcal::test::TemporaryDirectory temporaryDirectory("hdrcal-profile-overwrite");
    const std::filesystem::path destination = temporaryDirectory.path() / "hdrcal.lua";

    HDRCAL_REQUIRE(!hdrcal::saveProfileToPath(makeValidProfile(), destination).has_value());

    hdrcal::CalibrationProfile replacement = makeValidProfile();
    replacement.maximumLuminanceNits = 900;
    replacement.maximumFullFrameLuminanceNits = 450;
    HDRCAL_REQUIRE(!hdrcal::saveProfileToPath(replacement, destination).has_value());

    std::ifstream input(destination);
    const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    HDRCAL_REQUIRE(content.find("max_luminance = 900,") != std::string::npos);
    HDRCAL_REQUIRE(content.find("max_luminance = 600,") == std::string::npos);
}

HDRCAL_TEST(testSaveProfileToPath_InvalidProfile_DoesNotCreateFile) {
    const hdrcal::test::TemporaryDirectory temporaryDirectory("hdrcal-profile-invalid");
    const std::filesystem::path destination = temporaryDirectory.path() / "hdrcal.lua";

    hdrcal::CalibrationProfile invalid = makeValidProfile();
    invalid.maximumLuminanceNits = 0;
    HDRCAL_REQUIRE(hdrcal::saveProfileToPath(invalid, destination).has_value());
    HDRCAL_REQUIRE(!std::filesystem::exists(destination));
}

HDRCAL_TEST(testFormatUtcTimestamp_KnownInstant_FormatsIso8601) {
    const auto epoch = std::chrono::system_clock::from_time_t(0);
    HDRCAL_REQUIRE_EQ(hdrcal::formatUtcTimestamp(epoch), std::string("1970-01-01T00:00:00Z"));
}
