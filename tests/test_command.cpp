// Unit tests for direct process execution and output capture.

#include "hdrcal/command.hpp"
#include "test_framework.hpp"

#include <string>

HDRCAL_TEST(testRunCommand_CapturesStdoutStderrAndExitCode) {
    const hdrcal::CommandResult result = hdrcal::runCommand({"/bin/sh", "-c", "printf ola; printf erro >&2; exit 3"});

    HDRCAL_REQUIRE_EQ(result.exitCode, 3);
    HDRCAL_REQUIRE_EQ(result.standardOutput, std::string("ola"));
    HDRCAL_REQUIRE_EQ(result.standardError, std::string("erro"));
}

HDRCAL_TEST(testRunCommand_EmptyArguments_ReturnsError) {
    const hdrcal::CommandResult result = hdrcal::runCommand({});
    HDRCAL_REQUIRE(result.exitCode < 0);
    HDRCAL_REQUIRE(!result.standardError.empty());
}

HDRCAL_TEST(testRunCommand_NonexistentBinary_ReturnsExit127) {
    const hdrcal::CommandResult result = hdrcal::runCommand({"hdrcal-command-that-does-not-exist"});
    HDRCAL_REQUIRE_EQ(result.exitCode, 127);
}
