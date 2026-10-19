// Unit tests for the five HDR preconditions with injected commands and KMS data.

#include "hdrcal/preflight.hpp"
#include "test_framework.hpp"

#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace {

struct FakeEnvironment {
    bool monitorPresent = true;
    bool monitorEnabled = true;
    bool automaticHdrDisabled = true;
    bool directScanoutDisabled = true;
    int maximumBitsPerComponent = 10;
    int hdrEotf = 2;
    bool hdrEotfPresent = true;
    std::string framebufferFormat = "XRGB2101010";
};

hdrcal::CommandExecutor makeExecutor(FakeEnvironment environment) {
    return [environment](const std::vector<std::string>& arguments) -> hdrcal::CommandResult {
        if (arguments.size() >= 3 && arguments.at(1) == "getoption") {
            if (arguments.at(2) == "render:cm_auto_hdr")
                return hdrcal::CommandResult{.exitCode = 0,
                                             .standardOutput = "int: " + std::string(environment.automaticHdrDisabled ? "0" : "1") + "\nset: true\n",
                                             .standardError = {}};
            if (arguments.at(2) == "render:direct_scanout")
                return hdrcal::CommandResult{.exitCode = 0,
                                             .standardOutput = "int: " + std::string(environment.directScanoutDisabled ? "0" : "1") + "\nset: true\n",
                                             .standardError = {}};
        }

        if (arguments.size() >= 2 && arguments.at(1) == "monitors") {
            if (!environment.monitorPresent)
                return hdrcal::CommandResult{.exitCode = 0, .standardOutput = "[]", .standardError = {}};

            return hdrcal::CommandResult{
                .exitCode = 0,
                .standardOutput = "[{\"name\":\"DP-2\",\"currentFormat\":\"" + environment.framebufferFormat +
                                  "\",\"colorManagementPreset\":\"hdr\",\"disabled\":" + std::string(environment.monitorEnabled ? "false" : "true") + "}]",
                .standardError = {}};
        }

        return hdrcal::CommandResult{.exitCode = 0, .standardOutput = "ok\n", .standardError = {}};
    };
}

hdrcal::KmsConnectorReader makeKmsReader(FakeEnvironment environment) {
    return [environment](const std::string& outputName) -> hdrcal::KmsReadResult {
        if (!environment.monitorPresent)
            return {std::nullopt, "conector KMS ativo '" + outputName + "' não encontrado"};

        hdrcal::KmsConnectorState state;
        state.connectorName = outputName;
        state.maximumBitsPerComponent = environment.maximumBitsPerComponent;
        if (environment.hdrEotfPresent)
            state.hdrEotf = environment.hdrEotf;

        return {state, std::nullopt};
    };
}

} // namespace

HDRCAL_TEST(testCheckHdrPreconditions_AllRequirementsMet_PassesAllFiveChecks) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-preflight-pass");
    const FakeEnvironment environment;

    const hdrcal::PreflightResult result =
        hdrcal::checkHdrPreconditions(hdrcal::HyprController(makeExecutor(environment)), "DP-2", makeKmsReader(environment), configDirectory.path());

    HDRCAL_REQUIRE_EQ(result.checks.size(), std::size_t{5});
    HDRCAL_REQUIRE(result.allPassed());
}

HDRCAL_TEST(testCheckHdrPreconditions_EotfNotPq_FailsFirstCheck) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-preflight-eotf");
    FakeEnvironment environment;
    environment.hdrEotf = 0;

    const hdrcal::PreflightResult result =
        hdrcal::checkHdrPreconditions(hdrcal::HyprController(makeExecutor(environment)), "DP-2", makeKmsReader(environment), configDirectory.path());

    HDRCAL_REQUIRE_EQ(result.checks.size(), std::size_t{5});
    HDRCAL_REQUIRE(!result.checks.at(0).passed);
    HDRCAL_REQUIRE(!result.allPassed());
}

HDRCAL_TEST(testCheckHdrPreconditions_MetadataMissing_FailsFirstCheck) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-preflight-metadata");
    FakeEnvironment environment;
    environment.hdrEotfPresent = false;

    const hdrcal::PreflightResult result =
        hdrcal::checkHdrPreconditions(hdrcal::HyprController(makeExecutor(environment)), "DP-2", makeKmsReader(environment), configDirectory.path());

    HDRCAL_REQUIRE(!result.checks.at(0).passed);
}

HDRCAL_TEST(testCheckHdrPreconditions_EightBitsPerComponent_FailsSecondCheck) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-preflight-bpc");
    FakeEnvironment environment;
    environment.maximumBitsPerComponent = 8;

    const hdrcal::PreflightResult result =
        hdrcal::checkHdrPreconditions(hdrcal::HyprController(makeExecutor(environment)), "DP-2", makeKmsReader(environment), configDirectory.path());

    HDRCAL_REQUIRE(result.checks.at(0).passed);
    HDRCAL_REQUIRE(!result.checks.at(1).passed);
}

HDRCAL_TEST(testCheckHdrPreconditions_EightBitFramebuffer_FailsSecondCheck) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-preflight-format");
    FakeEnvironment environment;
    environment.framebufferFormat = "XRGB8888";

    const hdrcal::PreflightResult result =
        hdrcal::checkHdrPreconditions(hdrcal::HyprController(makeExecutor(environment)), "DP-2", makeKmsReader(environment), configDirectory.path());

    HDRCAL_REQUIRE(!result.checks.at(1).passed);
}

HDRCAL_TEST(testCheckHdrPreconditions_AutomaticHdrEnabled_FailsThirdCheck) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-preflight-auto-hdr");
    FakeEnvironment environment;
    environment.automaticHdrDisabled = false;

    const hdrcal::PreflightResult result =
        hdrcal::checkHdrPreconditions(hdrcal::HyprController(makeExecutor(environment)), "DP-2", makeKmsReader(environment), configDirectory.path());

    HDRCAL_REQUIRE(!result.checks.at(2).passed);
}

HDRCAL_TEST(testCheckHdrPreconditions_ActiveIccConfiguration_FailsFourthCheck) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-preflight-icc");
    {
        std::ofstream configFile(configDirectory.path() / "monitors.lua");
        configFile << "hl.monitor({ output = \"DP-2\", icc = \"/tmp/display.icc\" })\n";
    }

    const FakeEnvironment environment;
    const hdrcal::PreflightResult result =
        hdrcal::checkHdrPreconditions(hdrcal::HyprController(makeExecutor(environment)), "DP-2", makeKmsReader(environment), configDirectory.path());

    HDRCAL_REQUIRE(!result.checks.at(3).passed);
    HDRCAL_REQUIRE(result.checks.at(3).detail.find("display.icc") != std::string::npos);
}

HDRCAL_TEST(testCheckHdrPreconditions_CommentedIccConfiguration_PassesFourthCheck) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-preflight-icc-comment");
    {
        std::ofstream configFile(configDirectory.path() / "monitors.lua");
        configFile << "-- hl.monitor({ output = \"DP-2\", icc = \"/tmp/display.icc\" })\n";
        configFile << "hl.monitor({ output = \"DP-2\", bitdepth = 10, cm = \"hdr\" })\n";
    }

    const FakeEnvironment environment;
    const hdrcal::PreflightResult result =
        hdrcal::checkHdrPreconditions(hdrcal::HyprController(makeExecutor(environment)), "DP-2", makeKmsReader(environment), configDirectory.path());

    HDRCAL_REQUIRE(result.checks.at(3).passed);
}

HDRCAL_TEST(testCheckHdrPreconditions_IccAfterInlineBlockComment_FailsFourthCheck) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-preflight-icc-block-comment");
    {
        std::ofstream configFile(configDirectory.path() / "monitors.lua");
        configFile << "--[[ bloco ]] hl.monitor({ output = \"DP-2\", icc = \"/tmp/display.icc\" })\n";
    }

    const FakeEnvironment environment;
    const hdrcal::PreflightResult result =
        hdrcal::checkHdrPreconditions(hdrcal::HyprController(makeExecutor(environment)), "DP-2", makeKmsReader(environment), configDirectory.path());

    HDRCAL_REQUIRE(!result.checks.at(3).passed);
}

HDRCAL_TEST(testCheckHdrPreconditions_EmptyIccConfiguration_PassesFourthCheck) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-preflight-icc-empty");
    {
        std::ofstream configFile(configDirectory.path() / "monitors.conf");
        configFile << "monitor=DP-2,2560x1440@120,0x0,1,bitdepth,10,cm,hdr,icc,\n";
    }

    const FakeEnvironment environment;
    const hdrcal::PreflightResult result =
        hdrcal::checkHdrPreconditions(hdrcal::HyprController(makeExecutor(environment)), "DP-2", makeKmsReader(environment), configDirectory.path());

    HDRCAL_REQUIRE(result.checks.at(3).passed);
}

HDRCAL_TEST(testCheckHdrPreconditions_DirectScanoutEnabled_FailsFifthCheck) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-preflight-scanout");
    FakeEnvironment environment;
    environment.directScanoutDisabled = false;

    const hdrcal::PreflightResult result =
        hdrcal::checkHdrPreconditions(hdrcal::HyprController(makeExecutor(environment)), "DP-2", makeKmsReader(environment), configDirectory.path());

    HDRCAL_REQUIRE(!result.checks.at(4).passed);
}

HDRCAL_TEST(testCheckHdrPreconditions_MonitorMissing_FailsSecondCheck) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-preflight-monitor-missing");
    FakeEnvironment environment;
    environment.monitorPresent = false;

    const hdrcal::PreflightResult result =
        hdrcal::checkHdrPreconditions(hdrcal::HyprController(makeExecutor(environment)), "DP-2", makeKmsReader(environment), configDirectory.path());

    HDRCAL_REQUIRE(!result.checks.at(0).passed);
    HDRCAL_REQUIRE(!result.checks.at(1).passed);
}

HDRCAL_TEST(testCheckHdrPreconditions_DisabledMonitor_FailsSecondCheck) {
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-preflight-monitor-disabled");
    FakeEnvironment environment;
    environment.monitorEnabled = false;

    const hdrcal::PreflightResult result =
        hdrcal::checkHdrPreconditions(hdrcal::HyprController(makeExecutor(environment)), "DP-2", makeKmsReader(environment), configDirectory.path());

    HDRCAL_REQUIRE(result.checks.at(0).passed);
    HDRCAL_REQUIRE(!result.checks.at(1).passed);
    HDRCAL_REQUIRE(result.checks.at(1).detail.find("desativado") != std::string::npos);
}
