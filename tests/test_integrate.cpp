// Unit tests for the pure monitor-rule editor, rule matching and transactional
// config writes used by `hdrcal integrate`.

#include "hdrcal/integrate.hpp"
#include "test_framework.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

void writeTextFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path);
    output << content;
}

constexpr std::string_view kMultilineRule = R"(hl.monitor({
    output = "DP-2",
    mode = "2560x1440@120",
    position = "0x0",
    scale = "1",
    bitdepth = 10,
    cm = "hdr",
})
)";

constexpr std::string_view kIntegratedRule = R"(local ok, hdrcal = pcall(require, "hdrcal")
local profile = ok and hdrcal and hdrcal.profiles and hdrcal.profiles["DP-2"] or nil

hl.monitor({
    output = "DP-2",
    mode = "2560x1440@120",
    position = "0x0",
    scale = "1",
    bitdepth = 10,
    cm = "hdr",
    min_luminance = profile and profile.min_luminance or nil,
    max_luminance = profile and profile.max_luminance or nil,
    max_avg_luminance = profile and profile.max_avg_luminance or nil,
})
)";

} // namespace

HDRCAL_TEST(testPlanMonitorRuleIntegration_MultilinePreservesFieldsAndAddsProfile) {
    const hdrcal::IntegrationPlan plan = hdrcal::planMonitorRuleIntegration(kMultilineRule, "DP-2");
    HDRCAL_REQUIRE(!plan.error.has_value());
    HDRCAL_REQUIRE(!plan.alreadyIntegrated);
    HDRCAL_REQUIRE(!plan.diff.empty());

    HDRCAL_REQUIRE(plan.updatedContent.find("local hdrcalOk, hdrcalModule = pcall(require, \"hdrcal\")") != std::string::npos);
    HDRCAL_REQUIRE(plan.updatedContent.find("hdrcalModule.profiles[\"DP-2\"]") != std::string::npos);
    HDRCAL_REQUIRE(plan.updatedContent.find("min_luminance = hdrcalProfile and hdrcalProfile.min_luminance or nil") != std::string::npos);
    HDRCAL_REQUIRE(plan.updatedContent.find("max_luminance = hdrcalProfile and hdrcalProfile.max_luminance or nil") != std::string::npos);
    HDRCAL_REQUIRE(plan.updatedContent.find("max_avg_luminance = hdrcalProfile and hdrcalProfile.max_avg_luminance or nil") != std::string::npos);
    HDRCAL_REQUIRE(plan.updatedContent.find("mode = \"2560x1440@120\"") != std::string::npos);
    HDRCAL_REQUIRE(plan.updatedContent.find("bitdepth = 10") != std::string::npos);
    HDRCAL_REQUIRE(plan.updatedContent.find("cm = \"hdr\"") != std::string::npos);
    HDRCAL_REQUIRE(plan.diff.find("+local hdrcalOk") != std::string::npos);
}

HDRCAL_TEST(testPlanMonitorRuleIntegration_SecondRunIsIdempotent) {
    const hdrcal::IntegrationPlan first = hdrcal::planMonitorRuleIntegration(kMultilineRule, "DP-2");
    HDRCAL_REQUIRE(!first.error.has_value());

    const hdrcal::IntegrationPlan second = hdrcal::planMonitorRuleIntegration(first.updatedContent, "DP-2");
    HDRCAL_REQUIRE(!second.error.has_value());
    HDRCAL_REQUIRE(second.alreadyIntegrated);
    HDRCAL_REQUIRE(second.diff.empty());
    HDRCAL_REQUIRE_EQ(second.updatedContent, first.updatedContent);
}

HDRCAL_TEST(testPlanMonitorRuleIntegration_AlreadyIntegratedW4Style_ProposesNoChange) {
    const hdrcal::IntegrationPlan plan = hdrcal::planMonitorRuleIntegration(kIntegratedRule, "DP-2");
    HDRCAL_REQUIRE(!plan.error.has_value());
    HDRCAL_REQUIRE(plan.alreadyIntegrated);
    HDRCAL_REQUIRE(plan.diff.empty());
    HDRCAL_REQUIRE_EQ(plan.updatedContent, std::string(kIntegratedRule));
}

HDRCAL_TEST(testPlanMonitorRuleIntegration_PartialFields_ReturnsError) {
    const std::string content = "hl.monitor({\n    output = \"DP-2\",\n    min_luminance = 0.02,\n})\n";
    const hdrcal::IntegrationPlan plan = hdrcal::planMonitorRuleIntegration(content, "DP-2");
    HDRCAL_REQUIRE(plan.error.has_value());
    HDRCAL_REQUIRE(plan.error->find("parte dos campos") != std::string::npos);
}

HDRCAL_TEST(testPlanMonitorRuleIntegration_SingleLineRule_InsertsInline) {
    const std::string content = "hl.monitor({ output = \"DP-2\", bitdepth = 10, cm = \"hdr\" })\n";
    const hdrcal::IntegrationPlan plan = hdrcal::planMonitorRuleIntegration(content, "DP-2");
    HDRCAL_REQUIRE(!plan.error.has_value());
    HDRCAL_REQUIRE(plan.updatedContent.find("cm = \"hdr\", min_luminance =") != std::string::npos);
    HDRCAL_REQUIRE(plan.updatedContent.find("max_avg_luminance = hdrcalProfile") != std::string::npos);
}

HDRCAL_TEST(testPlanMonitorRuleIntegration_MultilineWithoutTrailingComma_AddsSeparator) {
    const std::string content = "hl.monitor({\n    output = \"DP-2\",\n    cm = \"hdr\"\n})\n";
    const hdrcal::IntegrationPlan plan = hdrcal::planMonitorRuleIntegration(content, "DP-2");
    HDRCAL_REQUIRE(!plan.error.has_value());
    HDRCAL_REQUIRE(plan.updatedContent.find("cm = \"hdr\",\n    min_luminance =") != std::string::npos);
}

HDRCAL_TEST(testPlanMonitorRuleIntegration_CommentedFieldsAndCalls_AreIgnored) {
    const std::string content = "-- hl.monitor({ output = \"DP-2\", min_luminance = 1 })\n"
                                "hl.monitor({\n"
                                "    output = \"DP-2\",\n"
                                "    -- min_luminance = 999,\n"
                                "    cm = \"hdr\", -- max_luminance = 999\n"
                                "})\n";
    const hdrcal::IntegrationPlan plan = hdrcal::planMonitorRuleIntegration(content, "DP-2");
    HDRCAL_REQUIRE(!plan.error.has_value());
    HDRCAL_REQUIRE(!plan.alreadyIntegrated);
    HDRCAL_REQUIRE(plan.updatedContent.find("min_luminance = hdrcalProfile") != std::string::npos);

    // The commented-out call must not count as a second rule: the profile
    // locals are inserted only once.
    const std::size_t firstLocal = plan.updatedContent.find("local hdrcalOk");
    HDRCAL_REQUIRE(firstLocal != std::string::npos);
    HDRCAL_REQUIRE_EQ(plan.updatedContent.find("local hdrcalOk", firstLocal + 1), std::string::npos);
}

HDRCAL_TEST(testPlanMonitorRuleIntegration_MultipleRulesSameFile_ReturnsError) {
    const std::string content = "hl.monitor({ output = \"DP-2\" })\nhl.monitor({ output = \"DP-2\" })\n";
    const hdrcal::IntegrationPlan plan = hdrcal::planMonitorRuleIntegration(content, "DP-2");
    HDRCAL_REQUIRE(plan.error.has_value());
    HDRCAL_REQUIRE(plan.error->find("2 chamadas") != std::string::npos);
}

HDRCAL_TEST(testPlanMonitorRuleIntegration_MissingOutput_ReturnsError) {
    const hdrcal::IntegrationPlan plan = hdrcal::planMonitorRuleIntegration(kMultilineRule, "HDMI-A-1");
    HDRCAL_REQUIRE(plan.error.has_value());
    HDRCAL_REQUIRE(plan.error->find("nenhuma chamada") != std::string::npos);
}

HDRCAL_TEST(testFindMonitorRuleMatches_MultipleFiles_ReportsAll) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-integrate-matches");
    writeTextFile(configDirectory.path() / "a.lua", std::string(kMultilineRule));
    writeTextFile(configDirectory.path() / "b.lua", std::string(kMultilineRule));

    const hdrcal::HyprConfigLinesResult lines = hdrcal::readHyprConfigLines(configDirectory.path());
    HDRCAL_REQUIRE(!lines.error.has_value());

    const hdrcal::MonitorRuleMatchesResult matches = hdrcal::findMonitorRuleMatches(lines.lines, "DP-2");
    HDRCAL_REQUIRE_EQ(matches.matches.size(), std::size_t{2});
    HDRCAL_REQUIRE_EQ(matches.matches.at(0).syntax, hdrcal::MonitorRuleSyntax::Lua);
    HDRCAL_REQUIRE_EQ(matches.matches.at(1).syntax, hdrcal::MonitorRuleSyntax::Lua);
}

HDRCAL_TEST(testFindMonitorRuleMatches_LegacyRule_ReportsLegacySyntax) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-integrate-legacy");
    writeTextFile(configDirectory.path() / "monitors.conf", "monitor=DP-2,2560x1440@120,0x0,1,bitdepth,10,cm,hdr\n");

    const hdrcal::HyprConfigLinesResult lines = hdrcal::readHyprConfigLines(configDirectory.path());
    HDRCAL_REQUIRE(!lines.error.has_value());

    const hdrcal::MonitorRuleMatchesResult matches = hdrcal::findMonitorRuleMatches(lines.lines, "DP-2");
    HDRCAL_REQUIRE_EQ(matches.matches.size(), std::size_t{1});
    HDRCAL_REQUIRE_EQ(matches.matches.front().syntax, hdrcal::MonitorRuleSyntax::Legacy);
}

HDRCAL_TEST(testConfigEditTransaction_WriteAndCommit_KeepsBackup) {
    const hdrcal::test::TemporaryDirectory directory("hdrcal-integrate-write");
    const std::filesystem::path filePath = directory.path() / "monitors.lua";
    writeTextFile(filePath, "original\n");

    hdrcal::ConfigEditTransaction transaction(filePath, "original\n", "updated\n");
    HDRCAL_REQUIRE(!transaction.write().has_value());
    HDRCAL_REQUIRE_EQ(readTextFile(filePath), std::string("updated\n"));

    HDRCAL_REQUIRE(!transaction.backupPath().empty());
    HDRCAL_REQUIRE(std::filesystem::exists(transaction.backupPath()));
    HDRCAL_REQUIRE_EQ(readTextFile(transaction.backupPath()), std::string("original\n"));

    HDRCAL_REQUIRE(!transaction.commitAfterValidation([] { return std::nullopt; }).has_value());
    HDRCAL_REQUIRE_EQ(readTextFile(filePath), std::string("updated\n"));
}

HDRCAL_TEST(testConfigEditTransaction_ValidationFailure_RestoresOriginalAndKeepsBackup) {
    const hdrcal::test::TemporaryDirectory directory("hdrcal-integrate-rollback");
    const std::filesystem::path filePath = directory.path() / "monitors.lua";
    writeTextFile(filePath, "original\n");

    hdrcal::ConfigEditTransaction transaction(filePath, "original\n", "updated\n");
    HDRCAL_REQUIRE(!transaction.write().has_value());

    const std::optional<std::string> error = transaction.commitAfterValidation([] { return std::string("reload falhou"); });
    HDRCAL_REQUIRE(error.has_value());
    HDRCAL_REQUIRE(error->find("alteração desfeita") != std::string::npos);
    HDRCAL_REQUIRE_EQ(readTextFile(filePath), std::string("original\n"));
    HDRCAL_REQUIRE_EQ(readTextFile(transaction.backupPath()), std::string("original\n"));
}

HDRCAL_TEST(testConfigEditTransaction_DestructorRollsBackUncommittedWrite) {
    const hdrcal::test::TemporaryDirectory directory("hdrcal-integrate-raii");
    const std::filesystem::path filePath = directory.path() / "monitors.lua";
    writeTextFile(filePath, "original\n");

    {
        hdrcal::ConfigEditTransaction transaction(filePath, "original\n", "updated\n");
        HDRCAL_REQUIRE(!transaction.write().has_value());
        HDRCAL_REQUIRE_EQ(readTextFile(filePath), std::string("updated\n"));
    }

    HDRCAL_REQUIRE_EQ(readTextFile(filePath), std::string("original\n"));
}
