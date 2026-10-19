// Implements the FIFO key source and key line mapping.

#include "hdrcal/key_source.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace hdrcal {

std::optional<WizardKey> parseWizardKeyLine(std::string_view line) {
    if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);

    if (line == "left")
        return WizardKey::Left;
    if (line == "right")
        return WizardKey::Right;
    if (line == "shift-left")
        return WizardKey::CoarseLeft;
    if (line == "shift-right")
        return WizardKey::CoarseRight;
    if (line == "enter")
        return WizardKey::Enter;
    if (line == "escape")
        return WizardKey::Escape;
    return std::nullopt;
}

FifoKeySourceCreateResult FifoKeySource::create(const std::filesystem::path& fifoPath) {
    if (::mkfifo(fifoPath.c_str(), 0600) != 0 && errno != EEXIST)
        return {std::nullopt, "não foi possível criar o FIFO de teclas: " + std::string(std::strerror(errno))};

    const int fileDescriptor = ::open(fifoPath.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fileDescriptor < 0)
        return {std::nullopt, "não foi possível abrir o FIFO de teclas: " + std::string(std::strerror(errno))};

    FifoKeySource source;
    source.m_readFileDescriptor.reset(fileDescriptor);
    source.m_lineBuffer.clear();
    return {std::move(source), std::nullopt};
}

std::optional<WizardKey> FifoKeySource::poll(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        const std::size_t newlinePosition = m_lineBuffer.find('\n');
        if (newlinePosition != std::string::npos) {
            const std::string line = m_lineBuffer.substr(0, newlinePosition);
            m_lineBuffer.erase(0, newlinePosition + 1);

            if (const auto key = parseWizardKeyLine(line); key.has_value())
                return key;
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            return std::nullopt;

        pollfd descriptor{};
        descriptor.fd = m_readFileDescriptor.get();
        descriptor.events = POLLIN;

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        const int pollResult = ::poll(&descriptor, 1, static_cast<int>(remaining));
        if (pollResult < 0) {
            if (errno == EINTR)
                continue;
            return std::nullopt;
        }
        if (pollResult == 0)
            return std::nullopt;

        char buffer[1024];
        const ssize_t bytesRead = ::read(m_readFileDescriptor.get(), buffer, sizeof buffer);
        if (bytesRead > 0) {
            m_lineBuffer.append(buffer, static_cast<std::size_t>(bytesRead));
            continue;
        }
        if (bytesRead == 0)
            return std::nullopt;
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            continue;
        return std::nullopt;
    }
}

} // namespace hdrcal
