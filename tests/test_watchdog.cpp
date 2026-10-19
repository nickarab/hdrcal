// Unit tests for the post-crash restoration watchdog.

#include "hdrcal/process.hpp"
#include "hdrcal/watchdog.hpp"
#include "test_framework.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {

void waitForChildProcess(pid_t childPid) {
    int status = 0;
    while (::waitpid(childPid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        HDRCAL_REQUIRE(false);
    }
    HDRCAL_REQUIRE(WIFEXITED(status));
    HDRCAL_REQUIRE_EQ(WEXITSTATUS(status), 0);
}

} // namespace

HDRCAL_TEST(testSessionRestoreWatchdog_CrashPathRestoresAndKillsMpv) {
    const hdrcal::test::TemporaryDirectory temporaryDirectory("hdrcal-watchdog-crash");
    const std::filesystem::path markerPath = temporaryDirectory.path() / "restored.marker";
    const std::filesystem::path mpvPidPath = temporaryDirectory.path() / "mpv.pid";

    const pid_t childPid = ::fork();
    HDRCAL_REQUIRE(childPid >= 0);

    if (childPid == 0) {
        hdrcal::SessionRestoreWatchdogStartResult started = hdrcal::SessionRestoreWatchdog::start({"touch", markerPath.string()});
        if (!started.watchdog)
            ::_exit(2);

        hdrcal::SpawnedProcessResult mpv = hdrcal::spawnProcess({"/bin/sleep", "30"});
        if (!mpv.process)
            ::_exit(3);

        {
            std::ofstream pidOutput(mpvPidPath);
            pidOutput << static_cast<long long>(mpv.process->pid());
        }

        if (started.watchdog->reportMpvProcess(mpv.process->pid()).has_value())
            ::_exit(4);

        // Simulate a crash: exit without stopGracefully() and without destructors.
        ::_exit(0);
    }

    waitForChildProcess(childPid);

    bool markerExists = false;
    for (int attempt = 0; attempt < 80; ++attempt) {
        if (std::filesystem::exists(markerPath)) {
            markerExists = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    HDRCAL_REQUIRE(markerExists);

    long long mpvPid = 0;
    {
        std::ifstream pidInput(mpvPidPath);
        pidInput >> mpvPid;
    }
    HDRCAL_REQUIRE(mpvPid > 0);

    bool mpvStopped = false;
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (::kill(static_cast<pid_t>(mpvPid), 0) != 0) {
            mpvStopped = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (!mpvStopped)
        (void)::kill(static_cast<pid_t>(mpvPid), SIGKILL);

    HDRCAL_REQUIRE(mpvStopped);
}

HDRCAL_TEST(testSessionRestoreWatchdog_GracefulStopDoesNotRunRestore) {
    const hdrcal::test::TemporaryDirectory temporaryDirectory("hdrcal-watchdog-graceful");
    const std::filesystem::path markerPath = temporaryDirectory.path() / "restored.marker";

    const pid_t childPid = ::fork();
    HDRCAL_REQUIRE(childPid >= 0);

    if (childPid == 0) {
        hdrcal::SessionRestoreWatchdogStartResult started = hdrcal::SessionRestoreWatchdog::start({"touch", markerPath.string()});
        if (!started.watchdog)
            ::_exit(2);

        started.watchdog->stopGracefully();
        ::_exit(0);
    }

    waitForChildProcess(childPid);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    HDRCAL_REQUIRE(!std::filesystem::exists(markerPath));
}
