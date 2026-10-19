// Unit tests for the FIFO key source and key line mapping.

#include "hdrcal/key_source.hpp"
#include "test_framework.hpp"

#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <unistd.h>

HDRCAL_TEST(testParseWizardKeyLine_MapsScriptStrings) {
    HDRCAL_REQUIRE_EQ(hdrcal::parseWizardKeyLine("left").value(), hdrcal::WizardKey::Left);
    HDRCAL_REQUIRE_EQ(hdrcal::parseWizardKeyLine("right").value(), hdrcal::WizardKey::Right);
    HDRCAL_REQUIRE_EQ(hdrcal::parseWizardKeyLine("shift-left").value(), hdrcal::WizardKey::CoarseLeft);
    HDRCAL_REQUIRE_EQ(hdrcal::parseWizardKeyLine("shift-right").value(), hdrcal::WizardKey::CoarseRight);
    HDRCAL_REQUIRE_EQ(hdrcal::parseWizardKeyLine("enter").value(), hdrcal::WizardKey::Enter);
    HDRCAL_REQUIRE_EQ(hdrcal::parseWizardKeyLine("escape").value(), hdrcal::WizardKey::Escape);
    HDRCAL_REQUIRE(!hdrcal::parseWizardKeyLine("unknown").has_value());
    HDRCAL_REQUIRE(!hdrcal::parseWizardKeyLine("").has_value());
}

HDRCAL_TEST(testFifoKeySource_ReadsKeysInOrder) {
    const hdrcal::test::TemporaryDirectory temporaryDirectory("hdrcal-keys");
    const std::filesystem::path fifoPath = temporaryDirectory.path() / "keys.fifo";

    hdrcal::FifoKeySourceCreateResult created = hdrcal::FifoKeySource::create(fifoPath);
    HDRCAL_REQUIRE(created.source.has_value());
    HDRCAL_REQUIRE(!created.error.has_value());

    const int writer = ::open(fifoPath.c_str(), O_WRONLY | O_CLOEXEC);
    HDRCAL_REQUIRE(writer >= 0);
    const std::string payload = "left\nshift-right\nenter\n";
    HDRCAL_REQUIRE_EQ(::write(writer, payload.data(), payload.size()), static_cast<ssize_t>(payload.size()));
    ::close(writer);

    HDRCAL_REQUIRE_EQ(created.source->poll(std::chrono::milliseconds(500)).value(), hdrcal::WizardKey::Left);
    HDRCAL_REQUIRE_EQ(created.source->poll(std::chrono::milliseconds(500)).value(), hdrcal::WizardKey::CoarseRight);
    HDRCAL_REQUIRE_EQ(created.source->poll(std::chrono::milliseconds(500)).value(), hdrcal::WizardKey::Enter);
}

HDRCAL_TEST(testFifoKeySource_UnknownLinesAreDiscarded) {
    const hdrcal::test::TemporaryDirectory temporaryDirectory("hdrcal-keys-unknown");
    const std::filesystem::path fifoPath = temporaryDirectory.path() / "keys.fifo";

    hdrcal::FifoKeySourceCreateResult created = hdrcal::FifoKeySource::create(fifoPath);
    HDRCAL_REQUIRE(created.source.has_value());

    const int writer = ::open(fifoPath.c_str(), O_WRONLY | O_CLOEXEC);
    HDRCAL_REQUIRE(writer >= 0);
    const std::string payload = "unknown\nright\n";
    HDRCAL_REQUIRE_EQ(::write(writer, payload.data(), payload.size()), static_cast<ssize_t>(payload.size()));

    HDRCAL_REQUIRE_EQ(created.source->poll(std::chrono::milliseconds(500)).value(), hdrcal::WizardKey::Right);
    ::close(writer);
}

HDRCAL_TEST(testFifoKeySource_TimeoutReturnsNoKey) {
    const hdrcal::test::TemporaryDirectory temporaryDirectory("hdrcal-keys-timeout");
    const std::filesystem::path fifoPath = temporaryDirectory.path() / "keys.fifo";

    hdrcal::FifoKeySourceCreateResult created = hdrcal::FifoKeySource::create(fifoPath);
    HDRCAL_REQUIRE(created.source.has_value());
    HDRCAL_REQUIRE(!created.source->poll(std::chrono::milliseconds(50)).has_value());
}
