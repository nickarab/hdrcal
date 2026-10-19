// Implements the mpv JSON IPC client and response parsing.

#include "hdrcal/mpv.hpp"

#include "hdrcal/json.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace hdrcal {
namespace {

void appendJsonEscape(std::string& destination, char character) {
    switch (character) {
    case '"':
        destination += "\\\"";
        break;
    case '\\':
        destination += "\\\\";
        break;
    case '\b':
        destination += "\\b";
        break;
    case '\f':
        destination += "\\f";
        break;
    case '\n':
        destination += "\\n";
        break;
    case '\r':
        destination += "\\r";
        break;
    case '\t':
        destination += "\\t";
        break;
    default:
        if (static_cast<unsigned char>(character) < 0x20) {
            char encoded[8];
            std::snprintf(encoded, sizeof encoded, "\\u%04x", static_cast<unsigned int>(static_cast<unsigned char>(character)));
            destination += encoded;
        } else {
            destination.push_back(character);
        }
        break;
    }
}

} // namespace

std::string escapeJsonString(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());

    for (const char character : text)
        appendJsonEscape(escaped, character);

    return escaped;
}

std::string buildMpvCommandJson(const std::vector<std::string>& command, long long requestId) {
    std::string json = "{\"command\":[";
    for (std::size_t index = 0; index < command.size(); ++index) {
        if (index != 0)
            json.push_back(',');
        json.push_back('"');
        json += escapeJsonString(command[index]);
        json.push_back('"');
    }
    json += "],\"request_id\":";
    json += std::to_string(requestId);
    json.push_back('}');
    return json;
}

std::string buildMpvInstructionOverlayJson(std::string_view instruction, long long requestId) {
    std::string data = "{\\an2\\fs28}";
    for (const char character : instruction) {
        if (character == '\n')
            data += "\\N";
        else
            data.push_back(character);
    }

    std::string json = "{\"command\":{\"name\":\"osd-overlay\",\"id\":1,\"format\":\"ass-events\",\"data\":\"";
    json += escapeJsonString(data);
    json += "\"},\"request_id\":";
    json += std::to_string(requestId);
    json.push_back('}');
    return json;
}

MpvResponse parseMpvResponseLine(std::string_view line, long long expectedRequestId) {
    const JsonParseResult parsed = parseJson(line);
    if (!parsed.value)
        return MpvResponse{.kind = MpvResponseKind::Invalid, .error = std::nullopt, .detail = "JSON inválido do mpv: " + parsed.error};

    const JsonValue* requestIdValue = parsed.value->find("request_id");
    if (!requestIdValue || !requestIdValue->asNumber())
        return MpvResponse{.kind = MpvResponseKind::Unrelated, .error = std::nullopt, .detail = {}};

    if (static_cast<long long>(*requestIdValue->asNumber()) != expectedRequestId)
        return MpvResponse{.kind = MpvResponseKind::Unrelated, .error = std::nullopt, .detail = {}};

    const JsonValue* errorValue = parsed.value->find("error");
    if (!errorValue || !errorValue->asString())
        return MpvResponse{.kind = MpvResponseKind::Invalid, .error = std::nullopt, .detail = "resposta do mpv sem campo 'error'"};

    if (*errorValue->asString() == "success")
        return MpvResponse{.kind = MpvResponseKind::MatchingResponse, .error = std::nullopt, .detail = {}};

    return MpvResponse{.kind = MpvResponseKind::MatchingResponse, .error = *errorValue->asString(), .detail = {}};
}

MpvIpcClient::~MpvIpcClient() = default;

std::optional<std::string> MpvIpcClient::connectToSocket(const std::filesystem::path& socketPath, std::chrono::milliseconds timeout) {
    const std::string path = socketPath.string();
    if (path.size() >= sizeof(sockaddr_un::sun_path))
        return "caminho do socket IPC do mpv é longo demais";

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        UniqueFileDescriptor socketDescriptor(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
        if (socketDescriptor.get() < 0)
            return "não foi possível criar o socket IPC: " + std::string(std::strerror(errno));

        if (::connect(socketDescriptor.get(), reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0) {
            m_socket = std::move(socketDescriptor);
            m_readBuffer.clear();
            return std::nullopt;
        }

        if (std::chrono::steady_clock::now() >= deadline)
            return "não foi possível conectar ao socket IPC do mpv: " + std::string(std::strerror(errno));

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

bool MpvIpcClient::isConnected() const {
    return m_socket.get() >= 0;
}

std::optional<std::string> MpvIpcClient::sendRaw(std::string_view data) {
    if (!isConnected())
        return "socket IPC do mpv não está conectado";

    std::size_t bytesSent = 0;
    while (bytesSent < data.size()) {
        const ssize_t sent = ::send(m_socket.get(), data.data() + bytesSent, data.size() - bytesSent, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR)
                continue;
            return "falha ao enviar comando ao mpv: " + std::string(std::strerror(errno));
        }
        bytesSent += static_cast<std::size_t>(sent);
    }

    return std::nullopt;
}

std::optional<std::string> MpvIpcClient::waitForResponse(long long requestId) {
    const auto deadline = std::chrono::steady_clock::now() + m_responseTimeout;

    while (true) {
        std::size_t newlinePosition = m_readBuffer.find('\n');
        while (newlinePosition != std::string::npos) {
            const std::string line = m_readBuffer.substr(0, newlinePosition);
            m_readBuffer.erase(0, newlinePosition + 1);

            const MpvResponse response = parseMpvResponseLine(line, requestId);
            if (response.kind == MpvResponseKind::Invalid)
                return response.detail;
            if (response.kind == MpvResponseKind::MatchingResponse)
                return response.error;

            newlinePosition = m_readBuffer.find('\n');
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            return "tempo esgotado aguardando resposta do mpv";

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        pollfd descriptor{};
        descriptor.fd = m_socket.get();
        descriptor.events = POLLIN;

        const int pollResult = ::poll(&descriptor, 1, static_cast<int>(remaining));
        if (pollResult < 0) {
            if (errno == EINTR)
                continue;
            return "falha ao aguardar resposta do mpv: " + std::string(std::strerror(errno));
        }
        if (pollResult == 0)
            return "tempo esgotado aguardando resposta do mpv";

        char buffer[4096];
        const ssize_t bytesRead = ::recv(m_socket.get(), buffer, sizeof buffer, 0);
        if (bytesRead > 0) {
            m_readBuffer.append(buffer, static_cast<std::size_t>(bytesRead));
            continue;
        }
        if (bytesRead == 0)
            return "mpv fechou a conexão IPC";
        if (errno == EINTR)
            continue;
        return "falha ao ler resposta do mpv: " + std::string(std::strerror(errno));
    }
}

std::optional<std::string> MpvIpcClient::sendCommand(const std::vector<std::string>& command) {
    const long long requestId = m_nextRequestId++;
    const std::string json = buildMpvCommandJson(command, requestId) + "\n";

    if (const auto sendError = sendRaw(json); sendError.has_value())
        return sendError;

    return waitForResponse(requestId);
}

std::optional<std::string> MpvIpcClient::loadFile(const std::filesystem::path& patternPath) {
    return sendCommand({"loadfile", patternPath.string(), "replace"});
}

std::optional<std::string> MpvIpcClient::showText(std::string_view text, int durationMilliseconds) {
    return sendCommand({"show-text", std::string(text), std::to_string(durationMilliseconds)});
}

std::optional<std::string> MpvIpcClient::showPermanentInstruction(std::string_view instruction) {
    const long long requestId = m_nextRequestId++;
    if (const auto sendError = sendRaw(buildMpvInstructionOverlayJson(instruction, requestId) + "\n"); sendError.has_value())
        return sendError;

    return waitForResponse(requestId);
}

std::optional<std::string> MpvIpcClient::quit() {
    return sendRaw(buildMpvCommandJson({"quit"}, m_nextRequestId++) + "\n");
}

} // namespace hdrcal
