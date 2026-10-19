// Move-only RAII owner for POSIX file descriptors.

#pragma once

#include <unistd.h>

namespace hdrcal {

// Move-only owner of a POSIX file descriptor. Closing twice is impossible; the
// destructor closes the descriptor on every normal and error path.
class UniqueFileDescriptor {
public:
    explicit UniqueFileDescriptor(int fileDescriptor = -1) noexcept : m_fileDescriptor(fileDescriptor) {}
    ~UniqueFileDescriptor() {
        reset();
    }

    UniqueFileDescriptor(const UniqueFileDescriptor&) = delete;
    UniqueFileDescriptor& operator=(const UniqueFileDescriptor&) = delete;

    UniqueFileDescriptor(UniqueFileDescriptor&& other) noexcept : m_fileDescriptor(other.m_fileDescriptor) {
        other.m_fileDescriptor = -1;
    }

    UniqueFileDescriptor& operator=(UniqueFileDescriptor&& other) noexcept {
        if (this == &other)
            return *this;
        reset();
        m_fileDescriptor = other.m_fileDescriptor;
        other.m_fileDescriptor = -1;
        return *this;
    }

    [[nodiscard]] int get() const noexcept {
        return m_fileDescriptor;
    }

    int release() noexcept {
        const int released = m_fileDescriptor;
        m_fileDescriptor = -1;
        return released;
    }

    void reset(int fileDescriptor = -1) noexcept {
        if (m_fileDescriptor >= 0)
            ::close(m_fileDescriptor);
        m_fileDescriptor = fileDescriptor;
    }

private:
    int m_fileDescriptor;
};

} // namespace hdrcal
