// Post-crash restoration watchdog for the temporary calibration rule.

#pragma once

#include "hdrcal/unique_fd.hpp"

#include <optional>
#include <string>
#include <sys/types.h>
#include <vector>

namespace hdrcal {

struct SessionRestoreWatchdogStartResult;

// Detached child process that waits for the calibration session to disappear.
// On a graceful stop it exits silently. If the session dies (crash, SIGKILL,
// uncaught failure), it terminates the mpv child and runs `hyprctl reload` so
// the temporary luminance rule never survives the session.
class SessionRestoreWatchdog {
public:
    SessionRestoreWatchdog() = default;
    ~SessionRestoreWatchdog();

    SessionRestoreWatchdog(const SessionRestoreWatchdog&) = delete;
    SessionRestoreWatchdog& operator=(const SessionRestoreWatchdog&) = delete;
    SessionRestoreWatchdog(SessionRestoreWatchdog&&) noexcept = default;
    SessionRestoreWatchdog& operator=(SessionRestoreWatchdog&&) noexcept = default;

    // Starts the detached watchdog. The restore command is executed verbatim
    // (no shell) if the session disappears without stopGracefully().
    [[nodiscard]] static SessionRestoreWatchdogStartResult start(std::vector<std::string> restoreCommand);

    // Informs the watchdog about the mpv child so it can terminate it on crash.
    [[nodiscard]] std::optional<std::string> reportMpvProcess(pid_t mpvPid);

    // Normal completion: the session already restored the monitor rule.
    void stopGracefully();

    // Leaves the watchdog armed; it will restore when this process exits.
    void abandon();

    [[nodiscard]] bool isActive() const;

private:
    explicit SessionRestoreWatchdog(UniqueFileDescriptor controlSocket) : m_controlSocket(std::move(controlSocket)) {}

    UniqueFileDescriptor m_controlSocket;
};

struct SessionRestoreWatchdogStartResult {
    std::optional<SessionRestoreWatchdog> watchdog;
    std::optional<std::string> error;
};

} // namespace hdrcal
