// Unit tests for the injectable hyprctl wrapper and the temporary rule guard.

#include "hdrcal/hypr.hpp"
#include "test_framework.hpp"

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

HDRCAL_TEST(testTemporaryLuminanceRule_RestoresOnDestruction) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>& arguments) {
        if (arguments.size() >= 2 && arguments.at(1) == "reload")
            return successfulResult("ok\n");
        return successfulResult("ok\n");
    };

    const hdrcal::HyprController controller(fake.executor());
    {
        hdrcal::TemporaryLuminanceRule guard(controller);
        HDRCAL_REQUIRE(!guard.apply("DP-2").has_value());
        HDRCAL_REQUIRE_EQ(fake.recordedCommands.size(), std::size_t{1});
    }

    HDRCAL_REQUIRE_EQ(fake.recordedCommands.size(), std::size_t{2});
    HDRCAL_REQUIRE_EQ(fake.recordedCommands.at(1).at(1), std::string("reload"));
}

HDRCAL_TEST(testTemporaryLuminanceRule_ExplicitRestoreDoesNotReloadTwice) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>&) { return successfulResult("ok\n"); };

    const hdrcal::HyprController controller(fake.executor());
    hdrcal::TemporaryLuminanceRule guard(controller);
    HDRCAL_REQUIRE(!guard.apply("DP-2").has_value());
    HDRCAL_REQUIRE(!guard.restore().has_value());

    HDRCAL_REQUIRE_EQ(fake.recordedCommands.size(), std::size_t{2});
}

HDRCAL_TEST(testTemporaryLuminanceRule_FailedExplicitRestore_RetriesOnDestruction) {
    FakeHyprCommands fake;
    fake.handler = [](const std::vector<std::string>& arguments) {
        if (arguments.size() >= 2 && arguments.at(1) == "reload")
            return hdrcal::CommandResult{.exitCode = 1, .standardOutput = {}, .standardError = "reload failed"};
        return successfulResult("ok\n");
    };

    const hdrcal::HyprController controller(fake.executor());
    {
        hdrcal::TemporaryLuminanceRule guard(controller);
        HDRCAL_REQUIRE(!guard.apply("DP-2").has_value());
        HDRCAL_REQUIRE(guard.restore().has_value());
        HDRCAL_REQUIRE_EQ(fake.recordedCommands.size(), std::size_t{2});
    }

    HDRCAL_REQUIRE_EQ(fake.recordedCommands.size(), std::size_t{3});
}
