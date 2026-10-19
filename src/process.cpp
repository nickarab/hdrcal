// Implements child-process spawning, liveness checks and termination.

#include "hdrcal/process.hpp"

#include "hdrcal/unique_fd.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace hdrcal {
namespace {

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

} // namespace

ChildProcess::ChildProcess(ChildProcess&& other) noexcept
    : m_pid(other.m_pid), m_lastStatus(other.m_lastStatus), m_lastExitCode(other.m_lastExitCode), m_hasStatus(other.m_hasStatus) {
    other.m_pid = -1;
    other.m_hasStatus = false;
}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
    if (this == &other)
        return *this;

    terminate();

    m_pid = other.m_pid;
    m_lastStatus = other.m_lastStatus;
    m_lastExitCode = other.m_lastExitCode;
    m_hasStatus = other.m_hasStatus;
    other.m_pid = -1;
    other.m_hasStatus = false;
    return *this;
}

ChildProcess::~ChildProcess() {
    terminate();
}

pid_t ChildProcess::pid() const {
    return m_pid;
}

bool ChildProcess::isRunning() {
    if (m_pid <= 0)
        return false;

    int status = 0;
    const pid_t result = ::waitpid(m_pid, &status, WNOHANG);
    if (result == 0)
        return true;

    if (result == m_pid || (result < 0 && errno == ECHILD)) {
        m_lastStatus = status;
        if (WIFEXITED(status))
            m_lastExitCode = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            m_lastExitCode = 128 + WTERMSIG(status);
        else
            m_lastExitCode = -1;
        m_hasStatus = true;
        m_pid = -1;
        return false;
    }

    return true;
}

int ChildProcess::wait() {
    if (m_pid <= 0)
        return m_hasStatus ? m_lastExitCode : -1;

    int status = 0;
    while (::waitpid(m_pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        m_pid = -1;
        return -1;
    }

    m_lastStatus = status;
    if (WIFEXITED(status))
        m_lastExitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        m_lastExitCode = 128 + WTERMSIG(status);
    else
        m_lastExitCode = -1;
    m_hasStatus = true;
    m_pid = -1;

    return m_lastExitCode;
}

void ChildProcess::terminate() {
    if (m_pid <= 0)
        return;

    if (!isRunning())
        return;

    (void)::kill(m_pid, SIGTERM);

    for (int attempt = 0; attempt < 40; ++attempt) {
        if (!isRunning())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    (void)::kill(m_pid, SIGKILL);
    (void)wait();
}

SpawnedProcessResult spawnProcess(const std::vector<std::string>& arguments) {
    if (arguments.empty())
        return {std::nullopt, "vetor de argumentos vazio"};

    int execPipe[2] = {-1, -1};
    if (::pipe2(execPipe, O_CLOEXEC) != 0)
        return {std::nullopt, "não foi possível criar o pipe de execução: " + std::string(std::strerror(errno))};

    UniqueFileDescriptor execRead(execPipe[0]);
    UniqueFileDescriptor execWrite(execPipe[1]);

    std::vector<char*> rawArguments;
    rawArguments.reserve(arguments.size() + 1);
    for (const auto& argument : arguments)
        rawArguments.push_back(const_cast<char*>(argument.c_str()));
    rawArguments.push_back(nullptr);

    const pid_t childPid = ::fork();
    if (childPid < 0) {
        return {std::nullopt, "não foi possível criar o processo: " + std::string(std::strerror(errno))};
    }

    if (childPid == 0) {
        (void)::setpgid(0, 0);

        redirectStandardStreamsToDevNull();
        execRead.reset();

        ::execvp(rawArguments[0], rawArguments.data());

        const int execError = errno;
        (void)::write(execWrite.get(), &execError, sizeof execError);
        ::_exit(127);
    }

    execWrite.reset();

    int execError = 0;
    ssize_t bytesRead = 0;
    do {
        bytesRead = ::read(execRead.get(), &execError, sizeof execError);
    } while (bytesRead < 0 && errno == EINTR);

    if (bytesRead > 0) {
        int status = 0;
        while (::waitpid(childPid, &status, 0) < 0 && errno == EINTR) {
        }
        return {std::nullopt, "não foi possível executar '" + arguments.front() + "': " + std::string(std::strerror(execError))};
    }

    return {ChildProcess(childPid), std::nullopt};
}

} // namespace hdrcal
