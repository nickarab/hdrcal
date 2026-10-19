// Unit tests for child-process spawning, liveness and termination.

#include "hdrcal/process.hpp"
#include "test_framework.hpp"

#include <chrono>
#include <thread>

HDRCAL_TEST(testSpawnProcess_RunsToCompletionAndReportsExitCode) {
    hdrcal::SpawnedProcessResult spawned = hdrcal::spawnProcess({"/bin/sh", "-c", "exit 3"});
    HDRCAL_REQUIRE(spawned.process.has_value());
    HDRCAL_REQUIRE(!spawned.error.has_value());

    HDRCAL_REQUIRE_EQ(spawned.process->wait(), 3);
    HDRCAL_REQUIRE(!spawned.process->isRunning());
}

HDRCAL_TEST(testSpawnProcess_NonexistentBinary_ReturnsError) {
    const hdrcal::SpawnedProcessResult spawned = hdrcal::spawnProcess({"hdrcal-process-that-does-not-exist"});
    HDRCAL_REQUIRE(!spawned.process.has_value());
    HDRCAL_REQUIRE(spawned.error.has_value());
}

HDRCAL_TEST(testSpawnProcess_TerminateStopsLongRunningProcess) {
    hdrcal::SpawnedProcessResult spawned = hdrcal::spawnProcess({"/bin/sleep", "30"});
    HDRCAL_REQUIRE(spawned.process.has_value());
    HDRCAL_REQUIRE(spawned.process->isRunning());

    spawned.process->terminate();
    HDRCAL_REQUIRE(!spawned.process->isRunning());
}
