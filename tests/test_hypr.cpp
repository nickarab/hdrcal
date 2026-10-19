// Unit tests for the injectable hyprctl wrapper and the temporary rule guard.

#include "hdrcal/hypr.hpp"
#include "test_framework.hpp"

#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace {

class FakeHyprCommands {
public:
    std::function<hdrcal::CommandResult(const std::vector<std::string>&)> handler;
    std::vector<std::vector<std::string>> recordedCommands;

    hdrcal::CommandExecutor executor() {
        return [this](const std::vector<std::string>& arguments) {
            recordedCommands.push_back(arguments);
            return handler(arguments);
        };
    }
};

hdrcal::CommandResult successfulResult(std::string output) {
    return hdrcal::CommandResult{.exitCode = 0, .standardOutput = std::move(output), .standardError = {}};
}

std::string monitorsJson(const std::string& format = "XRGB2101010") {
    return R"([{"name":"DP-2","currentFormat":")" + format + R"(","colorManagementPreset":"hdr","disabled":false}])";
}

hdrcal::KmsConnectorReader makeStableKmsReader(int maximumBitsPerComponent = 10, int eotf = 2) {
    return [maximumBitsPerComponent, eotf](const std::string& outputName) -> hdrcal::KmsReadResult {
        hdrcal::KmsConnectorState state;
        state.connectorName = outputName;
        state.maximumBitsPerComponent = maximumBitsPerComponent;
        state.hdrEotf = eotf;
        return {state, std::nullopt};
    };
}

void writeMonitorRule(const std::filesystem::path& directory, const std::string& content) {
    std::ofstream file(directory / "monitors.lua");
    file << content;
}

} // namespace

HDRCAL_TEST(testReadOption_ParsesIntegerBooleanAndString) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>& arguments) {
        if (arguments.at(2) == "render:cm_auto_hdr")
            return successfulResult("int: 0\nset: true\n");
        if (arguments.at(2) == "render:direct_scanout")
            return successfulResult("bool: false\nset: true\n");
        return successfulResult("str: hdr\nset: false\n");
    };

    const hdrcal::HyprController controller(fake.executor());

    const auto integerOption = controller.readOption("render:cm_auto_hdr");
    HDRCAL_REQUIRE(integerOption.value.has_value());
    HDRCAL_REQUIRE_EQ(integerOption.value->type, hdrcal::HyprOptionValue::Type::Integer);
    HDRCAL_REQUIRE_EQ(integerOption.value->integerValue, 0LL);

    const auto booleanOption = controller.readOption("render:direct_scanout");
    HDRCAL_REQUIRE(booleanOption.value.has_value());
    HDRCAL_REQUIRE_EQ(booleanOption.value->type, hdrcal::HyprOptionValue::Type::Boolean);
    HDRCAL_REQUIRE(!booleanOption.value->booleanValue);

    const auto stringOption = controller.readOption("monitorv2:DP-2:cm");
    HDRCAL_REQUIRE(stringOption.value.has_value());
    HDRCAL_REQUIRE_EQ(stringOption.value->stringValue, std::string("hdr"));
}

HDRCAL_TEST(testReadOption_NoSuchOption_ReturnsError) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>&) { return successfulResult("no such option\n"); };

    const hdrcal::HyprController controller(fake.executor());
    const auto result = controller.readOption("render:does_not_exist");
    HDRCAL_REQUIRE(!result.value.has_value());
    HDRCAL_REQUIRE(result.error.has_value());
}

HDRCAL_TEST(testReadOption_CommandFailure_ReturnsError) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>&) {
        return hdrcal::CommandResult{.exitCode = 127, .standardOutput = {}, .standardError = "hyprctl: not found"};
    };

    const hdrcal::HyprController controller(fake.executor());
    const auto result = controller.readOption("render:cm_auto_hdr");
    HDRCAL_REQUIRE(!result.value.has_value());
    HDRCAL_REQUIRE(result.error.has_value());
}

HDRCAL_TEST(testListMonitors_ParsesNamesFormatsAndEnabledState) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>&) {
        return successfulResult(R"([{"name":"DP-2","currentFormat":"XRGB2101010","colorManagementPreset":"hdr","disabled":false},)"
                                R"({"name":"HDMI-A-1","currentFormat":"XRGB8888","colorManagementPreset":"srgb","disabled":true}])");
    };

    const hdrcal::HyprController controller(fake.executor());
    const auto result = controller.listMonitors();
    HDRCAL_REQUIRE(result.value.has_value());
    HDRCAL_REQUIRE_EQ(result.value->size(), std::size_t{2});

    HDRCAL_REQUIRE_EQ(result.value->at(0).name, std::string("DP-2"));
    HDRCAL_REQUIRE_EQ(result.value->at(0).currentFormat, std::string("XRGB2101010"));
    HDRCAL_REQUIRE_EQ(result.value->at(0).colorManagementPreset, std::string("hdr"));
    HDRCAL_REQUIRE(result.value->at(0).enabled);

    HDRCAL_REQUIRE_EQ(result.value->at(1).name, std::string("HDMI-A-1"));
    HDRCAL_REQUIRE(!result.value->at(1).enabled);
}

HDRCAL_TEST(testListMonitors_MalformedJson_ReturnsError) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>&) { return successfulResult("not json"); };

    const hdrcal::HyprController controller(fake.executor());
    const auto result = controller.listMonitors();
    HDRCAL_REQUIRE(!result.value.has_value());
    HDRCAL_REQUIRE(result.error.has_value());
}

HDRCAL_TEST(testReadVersion_ParsesVersionAndBuildSuffix) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>&) { return successfulResult("Hyprland 0.56.2 built from branch at commit abc\n"); };

    const hdrcal::HyprController controller(fake.executor());
    const hdrcal::HyprlandVersionResult result = controller.readVersion();
    HDRCAL_REQUIRE(result.value.has_value());
    HDRCAL_REQUIRE_EQ(result.value->major, 0);
    HDRCAL_REQUIRE_EQ(result.value->minor, 56);
    HDRCAL_REQUIRE_EQ(result.value->patch, 2);
    HDRCAL_REQUIRE_EQ(result.value->raw, std::string("0.56.2"));
}

HDRCAL_TEST(testReadVersion_UnrecognizedOutput_ReturnsError) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>&) { return successfulResult("not a Hyprland version\n"); };

    const hdrcal::HyprController controller(fake.executor());
    HDRCAL_REQUIRE(!controller.readVersion().value.has_value());
    HDRCAL_REQUIRE(controller.readVersion().error.has_value());
}

HDRCAL_TEST(testReadVersion_CommandFailure_ReturnsError) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>&) {
        return hdrcal::CommandResult{.exitCode = 127, .standardOutput = {}, .standardError = "hyprctl: not found"};
    };

    const hdrcal::HyprController controller(fake.executor());
    HDRCAL_REQUIRE(!controller.readVersion().value.has_value());
    HDRCAL_REQUIRE(controller.readVersion().error.has_value());
}

HDRCAL_TEST(testCompareHyprlandVersions_OrdersByMajorMinorPatch) {
    HDRCAL_REQUIRE(hdrcal::compareHyprlandVersions({.major = 0, .minor = 55, .patch = 0, .raw = {}}, {.major = 0, .minor = 56, .patch = 2, .raw = {}}) < 0);
    HDRCAL_REQUIRE(hdrcal::compareHyprlandVersions({.major = 0, .minor = 56, .patch = 2, .raw = {}}, {.major = 0, .minor = 56, .patch = 2, .raw = {}}) == 0);
    HDRCAL_REQUIRE(hdrcal::compareHyprlandVersions({.major = 1, .minor = 0, .patch = 0, .raw = {}}, {.major = 0, .minor = 56, .patch = 2, .raw = {}}) > 0);
}

HDRCAL_TEST(testReload_SuccessAndFailure_AreReported) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>&) { return successfulResult("ok\n"); };
    const hdrcal::HyprController successfulController(fake.executor());
    HDRCAL_REQUIRE(!successfulController.reload().has_value());

    fake.handler = [](const std::vector<std::string>&) { return hdrcal::CommandResult{.exitCode = 1, .standardOutput = {}, .standardError = "failed"}; };
    const hdrcal::HyprController failingController(fake.executor());
    HDRCAL_REQUIRE(failingController.reload().has_value());
}

HDRCAL_TEST(testApplyMonitorLuminanceRule_SendsRuntimeEvalWithCalibrationEndpoints) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>&) { return successfulResult("ok\n"); };

    const hdrcal::HyprController controller(fake.executor());
    HDRCAL_REQUIRE(!controller.applyMonitorLuminanceRule("DP-2").has_value());

    HDRCAL_REQUIRE_EQ(fake.recordedCommands.size(), std::size_t{1});
    HDRCAL_REQUIRE_EQ(fake.recordedCommands.at(0).at(0), std::string("hyprctl"));
    HDRCAL_REQUIRE_EQ(fake.recordedCommands.at(0).at(1), std::string("eval"));

    const std::string& code = fake.recordedCommands.at(0).at(2);
    HDRCAL_REQUIRE(code.find("output = \"DP-2\"") != std::string::npos);
    HDRCAL_REQUIRE(code.find("min_luminance = 0") != std::string::npos);
    HDRCAL_REQUIRE(code.find("max_luminance = 10000") != std::string::npos);
    HDRCAL_REQUIRE(code.find("max_avg_luminance = 10000") != std::string::npos);
}

HDRCAL_TEST(testApplyMonitorLuminanceRule_InvalidOutputName_DoesNotExecuteCommand) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>&) { return successfulResult("ok\n"); };

    const hdrcal::HyprController controller(fake.executor());
    HDRCAL_REQUIRE(controller.applyMonitorLuminanceRule("DP-2\"bad").has_value());
    HDRCAL_REQUIRE(fake.recordedCommands.empty());
}

HDRCAL_TEST(testTemporaryLuminanceRule_RestoreWithoutApply_DoesNotReload) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>&) { return successfulResult("ok\n"); };
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-hypr-restore-noop");

    const hdrcal::HyprController controller(fake.executor());
    hdrcal::TemporaryLuminanceRule guard(controller, makeStableKmsReader(), configDirectory.path());
    HDRCAL_REQUIRE(!guard.restore().has_value());
    HDRCAL_REQUIRE(fake.recordedCommands.empty());
}

HDRCAL_TEST(testTemporaryLuminanceRule_ExistingNameRule_AppliesAndRestoresOnDestruction) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>& arguments) {
        if (arguments.at(1) == "monitors")
            return successfulResult(monitorsJson());
        return successfulResult("ok\n");
    };
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-hypr-existing-rule");
    writeMonitorRule(configDirectory.path(), "hl.monitor({ output = \"DP-2\", bitdepth = 10, cm = \"hdr\" })\n");

    const hdrcal::HyprController controller(fake.executor());
    {
        hdrcal::TemporaryLuminanceRule guard(controller, makeStableKmsReader(), configDirectory.path());
        HDRCAL_REQUIRE(!guard.apply("DP-2").has_value());
        HDRCAL_REQUIRE_EQ(fake.recordedCommands.size(), std::size_t{3});
        HDRCAL_REQUIRE_EQ(fake.recordedCommands.at(1).at(1), std::string("eval"));
    }

    HDRCAL_REQUIRE_EQ(fake.recordedCommands.size(), std::size_t{4});
    HDRCAL_REQUIRE_EQ(fake.recordedCommands.at(3).at(1), std::string("reload"));
}

HDRCAL_TEST(testTemporaryLuminanceRule_DescOnlyRule_FailsBeforeTouchingSession) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>&) { return successfulResult("ok\n"); };
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-hypr-desc-rule");
    writeMonitorRule(configDirectory.path(), "hl.monitor({ output = \"desc:Technical Concepts Ltd 27G64 00002\", bitdepth = 10 })\n");

    const hdrcal::HyprController controller(fake.executor());
    hdrcal::TemporaryLuminanceRule guard(controller, makeStableKmsReader(), configDirectory.path());

    const std::optional<std::string> error = guard.apply("DP-2");
    HDRCAL_REQUIRE(error.has_value());
    HDRCAL_REQUIRE(error->find("output exatamente igual") != std::string::npos);
    HDRCAL_REQUIRE(fake.recordedCommands.empty());
}

HDRCAL_TEST(testTemporaryLuminanceRule_MissingNameRule_FailsBeforeTouchingSession) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>&) { return successfulResult("ok\n"); };
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-hypr-missing-rule");

    const hdrcal::HyprController controller(fake.executor());
    hdrcal::TemporaryLuminanceRule guard(controller, makeStableKmsReader(), configDirectory.path());

    HDRCAL_REQUIRE(guard.apply("DP-2").has_value());
    HDRCAL_REQUIRE(fake.recordedCommands.empty());
}

HDRCAL_TEST(testTemporaryLuminanceRule_PostCheckDegradation_RestoresAndReturnsError) {
    FakeHyprCommands fake;
    int monitorsCallCount = 0;
    fake.handler = [&monitorsCallCount](const std::vector<std::string>& arguments) {
        if (arguments.at(1) == "monitors") {
            ++monitorsCallCount;
            return successfulResult(monitorsJson(monitorsCallCount == 1 ? "XRGB2101010" : "XRGB8888"));
        }
        return successfulResult("ok\n");
    };

    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-hypr-degradation");
    writeMonitorRule(configDirectory.path(), "hl.monitor({ output = \"DP-2\", bitdepth = 10, cm = \"hdr\" })\n");

    int kmsCallCount = 0;
    const hdrcal::KmsConnectorReader kmsReader = [&kmsCallCount](const std::string& outputName) -> hdrcal::KmsReadResult {
        ++kmsCallCount;
        hdrcal::KmsConnectorState state;
        state.connectorName = outputName;
        state.maximumBitsPerComponent = kmsCallCount == 1 ? 10 : 8;
        state.hdrEotf = kmsCallCount == 1 ? std::optional<int>{2} : std::optional<int>{0};
        return {state, std::nullopt};
    };

    const hdrcal::HyprController controller(fake.executor());
    {
        hdrcal::TemporaryLuminanceRule guard(controller, kmsReader, configDirectory.path());

        const std::optional<std::string> error = guard.apply("DP-2");
        HDRCAL_REQUIRE(error.has_value());
        HDRCAL_REQUIRE(error->find("degradou o estado HDR") != std::string::npos);
        HDRCAL_REQUIRE(error->find("sessão foi restaurada") != std::string::npos);

        HDRCAL_REQUIRE_EQ(fake.recordedCommands.size(), std::size_t{4});
        HDRCAL_REQUIRE_EQ(fake.recordedCommands.at(1).at(1), std::string("eval"));
        HDRCAL_REQUIRE_EQ(fake.recordedCommands.at(3).at(1), std::string("reload"));

        HDRCAL_REQUIRE(!guard.restore().has_value());
        HDRCAL_REQUIRE_EQ(fake.recordedCommands.size(), std::size_t{4});
    }

    // A successful verification rollback clears m_applied, so the destructor
    // must not reload a second time.
    HDRCAL_REQUIRE_EQ(fake.recordedCommands.size(), std::size_t{4});
}

HDRCAL_TEST(testTemporaryLuminanceRule_PostCheckReadFailure_RestoresAndReturnsError) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>& arguments) {
        if (arguments.at(1) == "monitors")
            return successfulResult(monitorsJson());
        return successfulResult("ok\n");
    };

    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-hypr-post-read-failure");
    writeMonitorRule(configDirectory.path(), "hl.monitor({ output = \"DP-2\", bitdepth = 10, cm = \"hdr\" })\n");

    int kmsCallCount = 0;
    const hdrcal::KmsConnectorReader kmsReader = [&kmsCallCount](const std::string& outputName) -> hdrcal::KmsReadResult {
        ++kmsCallCount;
        if (kmsCallCount > 1)
            return {std::nullopt, "KMS desapareceu"};
        hdrcal::KmsConnectorState state;
        state.connectorName = outputName;
        state.maximumBitsPerComponent = 10;
        state.hdrEotf = 2;
        return {state, std::nullopt};
    };

    const hdrcal::HyprController controller(fake.executor());
    hdrcal::TemporaryLuminanceRule guard(controller, kmsReader, configDirectory.path());

    const std::optional<std::string> error = guard.apply("DP-2");
    HDRCAL_REQUIRE(error.has_value());
    HDRCAL_REQUIRE(error->find("não foi possível verificar") != std::string::npos);
    HDRCAL_REQUIRE_EQ(fake.recordedCommands.at(fake.recordedCommands.size() - 1).at(1), std::string("reload"));
}

HDRCAL_TEST(testTemporaryLuminanceRule_ExplicitRestoreDoesNotReloadTwice) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>& arguments) {
        if (arguments.at(1) == "monitors")
            return successfulResult(monitorsJson());
        return successfulResult("ok\n");
    };
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-hypr-explicit-restore");
    writeMonitorRule(configDirectory.path(), "hl.monitor({ output = \"DP-2\", bitdepth = 10, cm = \"hdr\" })\n");

    const hdrcal::HyprController controller(fake.executor());
    hdrcal::TemporaryLuminanceRule guard(controller, makeStableKmsReader(), configDirectory.path());
    HDRCAL_REQUIRE(!guard.apply("DP-2").has_value());
    HDRCAL_REQUIRE(!guard.restore().has_value());

    HDRCAL_REQUIRE_EQ(fake.recordedCommands.size(), std::size_t{4});
}

HDRCAL_TEST(testTemporaryLuminanceRule_FailedExplicitRestore_RetriesOnDestruction) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>& arguments) {
        if (arguments.at(1) == "monitors")
            return successfulResult(monitorsJson());
        if (arguments.at(1) == "reload")
            return hdrcal::CommandResult{.exitCode = 1, .standardOutput = {}, .standardError = "reload failed"};
        return successfulResult("ok\n");
    };
    const hdrcal::test::TemporaryDirectory configDirectory("hdrcal-hypr-reload-retry");
    writeMonitorRule(configDirectory.path(), "hl.monitor({ output = \"DP-2\", bitdepth = 10, cm = \"hdr\" })\n");

    const hdrcal::HyprController controller(fake.executor());
    {
        hdrcal::TemporaryLuminanceRule guard(controller, makeStableKmsReader(), configDirectory.path());
        HDRCAL_REQUIRE(!guard.apply("DP-2").has_value());
        HDRCAL_REQUIRE(guard.restore().has_value());
        HDRCAL_REQUIRE_EQ(fake.recordedCommands.size(), std::size_t{4});
    }

    HDRCAL_REQUIRE_EQ(fake.recordedCommands.size(), std::size_t{5});
}
