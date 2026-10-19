// Implements fork/exec process execution with pipe draining and exit-code reporting.

#include "hdrcal/command.hpp"

#include "hdrcal/unique_fd.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace hdrcal {
namespace {

bool appendAvailableBytes(int fileDescriptor, std::string& destination, bool& stillOpen, std::string& error) {
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t bytesRead = ::read(fileDescriptor, buffer.data(), buffer.size());
        if (bytesRead > 0) {
            destination.append(buffer.data(), static_cast<std::size_t>(bytesRead));
            continue;
        }

        if (bytesRead == 0) {
            stillOpen = false;
            return true;
        }

        if (errno == EINTR)
            continue;

        error = std::string("falha ao ler a saída do comando: ") + std::strerror(errno);
        return false;
    }
}

bool drainCommandOutput(int standardOutputFd, int standardErrorFd, std::string& standardOutput, std::string& standardError, std::string& error) {
    std::array<pollfd, 2> pollDescriptors{};
    pollDescriptors[0] = pollfd{.fd = standardOutputFd, .events = POLLIN, .revents = 0};
    pollDescriptors[1] = pollfd{.fd = standardErrorFd, .events = POLLIN, .revents = 0};

    bool outputOpen = true;
    bool errorOpen = true;

    while (outputOpen || errorOpen) {
        const int pollResult = ::poll(pollDescriptors.data(), pollDescriptors.size(), -1);
        if (pollResult < 0) {
            if (errno == EINTR)
                continue;
            error = std::string("falha ao aguardar a saída do comando: ") + std::strerror(errno);
            return false;
        }

        if (outputOpen && (pollDescriptors[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            if (!appendAvailableBytes(standardOutputFd, standardOutput, outputOpen, error))
                return false;
            if (!outputOpen)
                pollDescriptors[0].fd = -1;
        }

        if (errorOpen && (pollDescriptors[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            if (!appendAvailableBytes(standardErrorFd, standardError, errorOpen, error))
                return false;
            if (!errorOpen)
                pollDescriptors[1].fd = -1;
        }
    }

    return true;
}

} // namespace

CommandResult runCommand(const std::vector<std::string>& arguments) {
    CommandResult result;

    if (arguments.empty()) {
        result.standardError = "vetor de argumentos vazio";
        return result;
    }

    std::array<int, 2> standardOutputPipe{};
    std::array<int, 2> standardErrorPipe{};
    if (::pipe2(standardOutputPipe.data(), O_CLOEXEC) != 0) {
        result.standardError = std::string("falha ao criar pipe: ") + std::strerror(errno);
        return result;
    }
    if (::pipe2(standardErrorPipe.data(), O_CLOEXEC) != 0) {
        result.standardError = std::string("falha ao criar pipe: ") + std::strerror(errno);
        ::close(standardOutputPipe[0]);
        ::close(standardOutputPipe[1]);
        return result;
    }

    UniqueFileDescriptor standardOutputRead(standardOutputPipe[0]);
    UniqueFileDescriptor standardOutputWrite(standardOutputPipe[1]);
    UniqueFileDescriptor standardErrorRead(standardErrorPipe[0]);
    UniqueFileDescriptor standardErrorWrite(standardErrorPipe[1]);

    std::vector<char*> rawArguments;
    rawArguments.reserve(arguments.size() + 1);
    for (const auto& argument : arguments)
        rawArguments.push_back(const_cast<char*>(argument.c_str()));
    rawArguments.push_back(nullptr);

    const pid_t childPid = ::fork();
    if (childPid < 0) {
        result.standardError = std::string("falha ao criar processo filho: ") + std::strerror(errno);
        return result;
    }

    if (childPid == 0) {
        // Child: only async-signal-safe calls from here until execvp.
        (void)::dup2(standardOutputWrite.get(), STDOUT_FILENO);
        (void)::dup2(standardErrorWrite.get(), STDERR_FILENO);

        standardOutputRead.reset();
        standardOutputWrite.reset();
        standardErrorRead.reset();
        standardErrorWrite.reset();

        ::execvp(rawArguments[0], rawArguments.data());
        ::_exit(127);
    }

    standardOutputWrite.reset();
    standardErrorWrite.reset();

    const int outputFd = standardOutputRead.release();
    const int errorFd = standardErrorRead.release();

    std::string outputError;
    const bool drained = drainCommandOutput(outputFd, errorFd, result.standardOutput, result.standardError, outputError);
    ::close(outputFd);
    ::close(errorFd);

    int status = 0;
    while (::waitpid(childPid, &status, 0) < 0) {
        if (errno != EINTR) {
            result.standardError += (result.standardError.empty() ? "" : "\n");
            result.standardError += std::string("falha ao aguardar o processo filho: ") + std::strerror(errno);
            result.exitCode = -1;
            return result;
        }
    }

    if (!drained) {
        result.exitCode = -1;
        result.standardError += (result.standardError.empty() ? "" : "\n");
        result.standardError += outputError;
        return result;
    }

    if (WIFEXITED(status))
        result.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        result.exitCode = 128 + WTERMSIG(status);
    else
        result.exitCode = -1;

    return result;
}

} // namespace hdrcal
