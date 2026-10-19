// Implements the detached post-crash restoration watchdog.

#include "hdrcal/watchdog.hpp"

#include "hdrcal/command.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace hdrcal {
namespace {

void ignoreTerminationSignals() {
    struct sigaction action{};
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    (void)::sigaction(SIGINT, &action, nullptr);
    (void)::sigaction(SIGTERM, &action, nullptr);
    (void)::sigaction(SIGHUP, &action, nullptr);
}

void redirectStandardStreamsToDevNull() {
    const int nullFileDescriptor = ::open("/dev/null", O_RDWR | O_CLOEXEC);
    if (nullFileDescriptor < 0)
        return;

    (void)::dup2(nullFileDescriptor, STDIN_FILENO);
    (void)::dup2(nullFileDescriptor, STDOUT_FILENO);
    (void)::dup2(nullFileDescriptor, STDERR_FILENO);

    if (nullFileDescriptor > STDERR_FILENO)
        ::close(nullFileDescriptor);
}

[[noreturn]] void runWatchdog(int controlFileDescriptor, std::vector<std::string> restoreCommand) {
    ignoreTerminationSignals();
    redirectStandardStreamsToDevNull();

    pid_t mpvPid = -1;
    std::string buffer;

    while (true) {
        char chunk[512];
        const ssize_t bytesRead = ::read(controlFileDescriptor, chunk, sizeof chunk);
        if (bytesRead > 0) {
            buffer.append(chunk, static_cast<std::size_t>(bytesRead));

            std::size_t newlinePosition = buffer.find('\n');
            while (newlinePosition != std::string::npos) {
                const std::string line = buffer.substr(0, newlinePosition);
                buffer.erase(0, newlinePosition + 1);

                if (line == "done")
                    ::_exit(0);

                if (line.starts_with("mpv ")) {
                    const std::string pidText = line.substr(4);
                    const auto parsedPid = std::strtol(pidText.c_str(), nullptr, 10);
                    if (parsedPid > 0)
                        mpvPid = static_cast<pid_t>(parsedPid);
                }

                newlinePosition = buffer.find('\n');
            }
            continue;
        }

        if (bytesRead == 0)
            break;

        if (errno == EINTR)
            continue;

        break;
    }

    if (mpvPid > 0)
        (void)::kill(mpvPid, SIGTERM);

    // Give mpv a moment to release the fullscreen surface before reloading the
    // monitor configuration.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const CommandResult result = runCommand(restoreCommand);
    ::_exit(result.exitCode == 0 ? 0 : 1);
}

} // namespace

SessionRestoreWatchdog::~SessionRestoreWatchdog() {
    abandon();
}

SessionRestoreWatchdogStartResult SessionRestoreWatchdog::start(std::vector<std::string> restoreCommand) {
    if (restoreCommand.empty())
        return {std::nullopt, "comando de restauração vazio"};

    int controlPair[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, controlPair) != 0)
        return {std::nullopt, "não foi possível criar o socket do watchdog: " + std::string(std::strerror(errno))};

    UniqueFileDescriptor parentSocket(controlPair[0]);
    UniqueFileDescriptor childSocket(controlPair[1]);

    const pid_t intermediatePid = ::fork();
    if (intermediatePid < 0)
        return {std::nullopt, "não foi possível criar o watchdog: " + std::string(std::strerror(errno))};

    if (intermediatePid == 0) {
        parentSocket.reset();

        const pid_t watchdogPid = ::fork();
        if (watchdogPid != 0)
            ::_exit(watchdogPid < 0 ? 1 : 0);

        (void)::setsid();
        runWatchdog(childSocket.release(), std::move(restoreCommand));
    }

    childSocket.reset();

    int status = 0;
    while (::waitpid(intermediatePid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        return {std::nullopt, "não foi possível aguardar o watchdog: " + std::string(std::strerror(errno))};
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return {std::nullopt, "não foi possível destacar o watchdog"};

    return {SessionRestoreWatchdog(std::move(parentSocket)), std::nullopt};
}

std::optional<std::string> SessionRestoreWatchdog::reportMpvProcess(pid_t mpvPid) {
    if (!isActive())
        return "watchdog de restauração não está ativo";
    if (mpvPid <= 0)
        return "pid do mpv inválido para o watchdog";

    const std::string message = "mpv " + std::to_string(static_cast<long long>(mpvPid)) + "\n";
    if (::send(m_controlSocket.get(), message.data(), message.size(), MSG_NOSIGNAL) < 0)
        return "não foi possível informar o mpv ao watchdog: " + std::string(std::strerror(errno));

    return std::nullopt;
}

void SessionRestoreWatchdog::stopGracefully() {
    if (!isActive())
        return;

    const std::string message = "done\n";
    (void)::send(m_controlSocket.get(), message.data(), message.size(), MSG_NOSIGNAL);
    m_controlSocket.reset();
}

void SessionRestoreWatchdog::abandon() {
    m_controlSocket.reset();
}

bool SessionRestoreWatchdog::isActive() const {
    return m_controlSocket.get() >= 0;
}

} // namespace hdrcal
