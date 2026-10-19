// Unit tests for the read-only Hyprland configuration scan.

#include "hdrcal/hypr_config.hpp"
#include "test_framework.hpp"

#include <fstream>
#include <string>

namespace {

void writeTextFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path);
    output << content;
}

} // namespace

HDRCAL_TEST(testFindMonitorRuleLocation_LuaMultilineExactOutput_Matches) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-config-lua-match");
    writeTextFile(configDirectory.path() / "monitors.lua", "hl.monitor({\n    output = \"DP-2\",\n    bitdepth = 10,\n    cm = \"hdr\",\n})\n");

    const hdrcal::HyprConfigLinesResult lines = hdrcal::readHyprConfigLines(configDirectory.path());
    HDRCAL_REQUIRE(!lines.error.has_value());

    const hdrcal::MonitorRuleLocation location = hdrcal::findMonitorRuleLocation(lines.lines, "DP-2");
    HDRCAL_REQUIRE(!location.error.has_value());
    HDRCAL_REQUIRE(location.location.has_value());
    HDRCAL_REQUIRE(location.location->find("monitors.lua:1") != std::string::npos);
}

HDRCAL_TEST(testFindMonitorRuleLocation_LuaCommentedExactOutput_DoesNotMatch) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-config-lua-comment");
    writeTextFile(configDirectory.path() / "monitors.lua", "-- hl.monitor({ output = \"DP-2\", bitdepth = 10 })\nhl.monitor({ output = \"HDMI-A-1\" })\n");

    const hdrcal::HyprConfigLinesResult lines = hdrcal::readHyprConfigLines(configDirectory.path());
    HDRCAL_REQUIRE(!lines.error.has_value());
    HDRCAL_REQUIRE(!hdrcal::findMonitorRuleLocation(lines.lines, "DP-2").location.has_value());
}

HDRCAL_TEST(testFindMonitorRuleLocation_LuaDescAndWildcardRules_DoNotMatch) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-config-desc");
    writeTextFile(configDirectory.path() / "monitors.lua", "hl.monitor({ output = \"desc:Technical Concepts Ltd 27G64 00002\", bitdepth = 10 })\n"
                                                           "hl.monitor({ output = \"\", bitdepth = 10 })\n");

    const hdrcal::HyprConfigLinesResult lines = hdrcal::readHyprConfigLines(configDirectory.path());
    HDRCAL_REQUIRE(!lines.error.has_value());
    HDRCAL_REQUIRE(!hdrcal::findMonitorRuleLocation(lines.lines, "DP-2").location.has_value());
}

HDRCAL_TEST(testFindMonitorRuleLocation_LuaOutputInsideString_DoesNotMatch) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-config-string");
    writeTextFile(configDirectory.path() / "monitors.lua", "local text = 'hl.monitor({ output = \"DP-2\" })'\n");

    const hdrcal::HyprConfigLinesResult lines = hdrcal::readHyprConfigLines(configDirectory.path());
    HDRCAL_REQUIRE(!lines.error.has_value());
    HDRCAL_REQUIRE(!hdrcal::findMonitorRuleLocation(lines.lines, "DP-2").location.has_value());
}

HDRCAL_TEST(testFindMonitorRuleLocation_LegacyConfExactOutput_Matches) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-config-legacy");
    writeTextFile(configDirectory.path() / "monitors.conf", "# monitor=DP-9,preferred,auto,1\nmonitor = DP-2, 2560x1440@120, 0x0, 1, bitdepth, 10, cm, hdr\n");

    const hdrcal::HyprConfigLinesResult lines = hdrcal::readHyprConfigLines(configDirectory.path());
    HDRCAL_REQUIRE(!lines.error.has_value());

    const hdrcal::MonitorRuleLocation location = hdrcal::findMonitorRuleLocation(lines.lines, "DP-2");
    HDRCAL_REQUIRE(location.location.has_value());
    HDRCAL_REQUIRE(location.location->find("monitors.conf:2") != std::string::npos);
}

HDRCAL_TEST(testFindMonitorRuleLocation_LegacyDescRule_DoesNotMatch) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-config-legacy-desc");
    writeTextFile(configDirectory.path() / "monitors.conf", "monitor=desc:Technical Concepts Ltd 27G64 00002,preferred,auto,1\n");

    const hdrcal::HyprConfigLinesResult lines = hdrcal::readHyprConfigLines(configDirectory.path());
    HDRCAL_REQUIRE(!lines.error.has_value());
    HDRCAL_REQUIRE(!hdrcal::findMonitorRuleLocation(lines.lines, "DP-2").location.has_value());
}

HDRCAL_TEST(testReadHyprConfigLines_MissingDirectory_ReturnsNoLinesAndNoError) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-config-missing");
    const hdrcal::HyprConfigLinesResult lines = hdrcal::readHyprConfigLines(configDirectory.path() / "does-not-exist");
    HDRCAL_REQUIRE(!lines.error.has_value());
    HDRCAL_REQUIRE(lines.lines.empty());
}
