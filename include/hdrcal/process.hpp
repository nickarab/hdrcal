// RAII child-process management for the mpv backend.

#pragma once

#include <optional>
#include <string>
#include <sys/types.h>
#include <vector>

namespace hdrcal {

struct SpawnedProcessResult;

class ChildProcess {
public:
    ChildProcess() = default;
    ~ChildProcess();

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&& other) noexcept;
    ChildProcess& operator=(ChildProcess&& other) noexcept;

    [[nodiscard]] pid_t pid() const;
    [[nodiscard]] bool isRunning();
    [[nodiscard]] int wait();

    // Sends SIGTERM, waits up to two seconds, then sends SIGKILL. Idempotent.
    void terminate();

private:
    explicit ChildProcess(pid_t processId) : m_pid(processId) {}

    pid_t m_pid = -1;
    int m_lastStatus = 0;
    int m_lastExitCode = -1;
    bool m_hasStatus = false;

    friend SpawnedProcessResult spawnProcess(const std::vector<std::string>& arguments);
};

struct SpawnedProcessResult {
    std::optional<ChildProcess> process;
    std::optional<std::string> error;
};

// Starts an argument vector in its own process group with stdin/stdout/stderr
// redirected to /dev/null. Detection of exec failures is done with a CLOEXEC
// pipe. Returns a user-facing error when the process cannot be started.
[[nodiscard]] SpawnedProcessResult spawnProcess(const std::vector<std::string>& arguments);

} // namespace hdrcal
