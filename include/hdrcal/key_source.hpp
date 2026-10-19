// FIFO-backed key source fed by the embedded mpv Lua script.

#pragma once

#include "hdrcal/unique_fd.hpp"
#include "hdrcal/wizard.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

namespace hdrcal {

struct FifoKeySourceCreateResult;

class FifoKeySource {
public:
    FifoKeySource() = default;

    FifoKeySource(const FifoKeySource&) = delete;
    FifoKeySource& operator=(const FifoKeySource&) = delete;
    FifoKeySource(FifoKeySource&&) noexcept = default;
    FifoKeySource& operator=(FifoKeySource&&) noexcept = default;

    // Creates the FIFO and opens its read end non-blocking. The mpv Lua script
    // opens the write end on the first key press.
    [[nodiscard]] static FifoKeySourceCreateResult create(const std::filesystem::path& fifoPath);

    // Waits up to timeout for one key. Unknown lines are discarded. Returns
    // std::nullopt on timeout, EOF or no complete line.
    [[nodiscard]] std::optional<WizardKey> poll(std::chrono::milliseconds timeout);

private:
    UniqueFileDescriptor m_readFileDescriptor;
    std::string m_lineBuffer;
};

struct FifoKeySourceCreateResult {
    std::optional<FifoKeySource> source;
    std::optional<std::string> error;
};

// Maps the exact strings sent by the Lua script to wizard keys.
[[nodiscard]] std::optional<WizardKey> parseWizardKeyLine(std::string_view line);

} // namespace hdrcal
