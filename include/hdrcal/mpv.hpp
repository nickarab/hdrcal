// JSON-over-Unix-socket client for mpv's IPC interface.

#pragma once

#include "hdrcal/unique_fd.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hdrcal {

// Escapes a string for JSON. Control characters are emitted as \u00XX.
[[nodiscard]] std::string escapeJsonString(std::string_view text);

// Builds one mpv IPC command line without the trailing newline:
// {"command":["loadfile","/tmp/a.png","replace"],"request_id":1}
[[nodiscard]] std::string buildMpvCommandJson(const std::vector<std::string>& command, long long requestId);

// Builds the named-argument form required by osd-overlay:
// {"command":{"name":"osd-overlay","id":1,...},"request_id":1}.
// mpv only enables named arguments when "command" is an object; a flat array
// rejects the integer id. Newlines in the instruction become ASS "\N" so the
// entire instruction stays one bottom-centered event.
[[nodiscard]] std::string buildMpvInstructionOverlayJson(std::string_view instruction, long long requestId);

enum class MpvResponseKind {
    MatchingResponse,
    Unrelated,
    Invalid,
};

struct MpvResponse {
    MpvResponseKind kind = MpvResponseKind::Invalid;
    std::optional<std::string> error;
    std::string detail;
};

// Parses one mpv IPC line. `Unrelated` means an event or a response to another
// request and must be skipped by the caller.
[[nodiscard]] MpvResponse parseMpvResponseLine(std::string_view line, long long expectedRequestId);

class MpvIpcClient {
public:
    MpvIpcClient() = default;
    ~MpvIpcClient();

    MpvIpcClient(const MpvIpcClient&) = delete;
    MpvIpcClient& operator=(const MpvIpcClient&) = delete;
    MpvIpcClient(MpvIpcClient&&) noexcept = default;
    MpvIpcClient& operator=(MpvIpcClient&&) noexcept = default;

    // Connects to the Unix socket, retrying until timeout because mpv creates
    // the socket after startup. Returns a user-facing error on failure.
    [[nodiscard]] std::optional<std::string> connectToSocket(const std::filesystem::path& socketPath, std::chrono::milliseconds timeout);

    [[nodiscard]] bool isConnected() const;

    // Sends a command and waits for the matching request_id response. Returns
    // std::nullopt on success.
    [[nodiscard]] std::optional<std::string> sendCommand(const std::vector<std::string>& command);

    [[nodiscard]] std::optional<std::string> loadFile(const std::filesystem::path& patternPath);
    [[nodiscard]] std::optional<std::string> showText(std::string_view text, int durationMilliseconds);

    // Adds or replaces the persistent bottom-centered instruction overlay.
    // Unlike show-text, this overlay has no timeout and remains visible until
    // the next call or until the IPC connection closes.
    [[nodiscard]] std::optional<std::string> showPermanentInstruction(std::string_view instruction);

    // quit may close the socket without a response, so this only checks that
    // the command bytes were sent.
    [[nodiscard]] std::optional<std::string> quit();

private:
    [[nodiscard]] std::optional<std::string> sendRaw(std::string_view data);
    [[nodiscard]] std::optional<std::string> waitForResponse(long long requestId);

    UniqueFileDescriptor m_socket;
    std::string m_readBuffer;
    long long m_nextRequestId = 1;
    std::chrono::milliseconds m_responseTimeout{3000};
};

} // namespace hdrcal
