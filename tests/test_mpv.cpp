// Unit tests for mpv JSON encoding, response parsing and IPC request/response.

#include "hdrcal/mpv.hpp"
#include "test_framework.hpp"

#include <cstring>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

HDRCAL_TEST(testBuildMpvCommandJson_EscapesStringsAndBuildsRequestId) {
    const std::string json = hdrcal::buildMpvCommandJson({"loadfile", "/tmp/a b\"c\\d.png", "replace"}, 7);
    HDRCAL_REQUIRE_EQ(json, std::string("{\"command\":[\"loadfile\",\"/tmp/a b\\\"c\\\\d.png\",\"replace\"],\"request_id\":7}"));
}

HDRCAL_TEST(testBuildMpvInstructionOverlayJson_UsesNamedArgumentsAndAssLineBreak) {
    const std::string json = hdrcal::buildMpvInstructionOverlayJson("Fase 1/3\nSetas = 1 código", 9);
    HDRCAL_REQUIRE_EQ(json, std::string("{\"command\":{\"name\":\"osd-overlay\",\"id\":1,\"format\":\"ass-events\",\"data\":\"{\\\\an2\\\\fs28}"
                                        "Fase 1/3\\\\NSetas = 1 código\"},\"request_id\":9}"));
}

HDRCAL_TEST(testEscapeJsonString_EscapesControlCharacters) {
    HDRCAL_REQUIRE_EQ(hdrcal::escapeJsonString("a\nb\tc"), std::string("a\\nb\\tc"));
    HDRCAL_REQUIRE_EQ(hdrcal::escapeJsonString(std::string("\x01")), std::string("\\u0001"));
}

HDRCAL_TEST(testParseMpvResponseLine_SuccessAndError) {
    const hdrcal::MpvResponse success = hdrcal::parseMpvResponseLine(R"({"request_id":3,"error":"success"})", 3);
    HDRCAL_REQUIRE_EQ(success.kind, hdrcal::MpvResponseKind::MatchingResponse);
    HDRCAL_REQUIRE(!success.error.has_value());

    const hdrcal::MpvResponse failure = hdrcal::parseMpvResponseLine(R"({"request_id":3,"error":"invalid parameter"})", 3);
    HDRCAL_REQUIRE_EQ(failure.kind, hdrcal::MpvResponseKind::MatchingResponse);
    HDRCAL_REQUIRE_EQ(failure.error.value_or(""), std::string("invalid parameter"));
}

HDRCAL_TEST(testParseMpvResponseLine_EventsAndOtherRequestsAreUnrelated) {
    HDRCAL_REQUIRE_EQ(hdrcal::parseMpvResponseLine(R"({"event":"start-file"})", 1).kind, hdrcal::MpvResponseKind::Unrelated);
    HDRCAL_REQUIRE_EQ(hdrcal::parseMpvResponseLine(R"({"request_id":2,"error":"success"})", 1).kind, hdrcal::MpvResponseKind::Unrelated);
}

HDRCAL_TEST(testParseMpvResponseLine_InvalidJson_ReturnsInvalid) {
    HDRCAL_REQUIRE_EQ(hdrcal::parseMpvResponseLine("not json", 1).kind, hdrcal::MpvResponseKind::Invalid);
}

namespace {

struct FakeMpvServer {
    hdrcal::test::TemporaryDirectory directory{"hdrcal-mpv"};
    std::filesystem::path socketPath = directory.path() / "mpv.sock";
    int serverFileDescriptor = -1;
    std::thread thread;

    FakeMpvServer() {
        serverFileDescriptor = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        HDRCAL_REQUIRE(serverFileDescriptor >= 0);

        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, socketPath.c_str(), socketPath.string().size() + 1);
        HDRCAL_REQUIRE_EQ(::bind(serverFileDescriptor, reinterpret_cast<const sockaddr*>(&address), sizeof address), 0);
        HDRCAL_REQUIRE_EQ(::listen(serverFileDescriptor, 1), 0);

        thread = std::thread([this] {
            const int client = ::accept(serverFileDescriptor, nullptr, nullptr);
            if (client < 0)
                return;

            std::string request;
            char buffer[4096];
            while (request.find('\n') == std::string::npos) {
                const ssize_t bytesRead = ::read(client, buffer, sizeof buffer);
                if (bytesRead <= 0)
                    break;
                request.append(buffer, static_cast<std::size_t>(bytesRead));
            }

            const std::string response = R"({"request_id":1,"error":"success"})"
                                         "\n";
            (void)::send(client, response.data(), response.size(), MSG_NOSIGNAL);
            ::close(client);
        });
    }

    ~FakeMpvServer() {
        if (serverFileDescriptor >= 0)
            ::shutdown(serverFileDescriptor, SHUT_RDWR);
        if (thread.joinable())
            thread.join();
        if (serverFileDescriptor >= 0)
            ::close(serverFileDescriptor);
    }
};

} // namespace

HDRCAL_TEST(testMpvIpcClient_SendsCommandAndWaitsForMatchingResponse) {
    FakeMpvServer server;

    hdrcal::MpvIpcClient client;
    HDRCAL_REQUIRE(!client.connectToSocket(server.socketPath, std::chrono::milliseconds(1000)).has_value());
    HDRCAL_REQUIRE(client.isConnected());
    HDRCAL_REQUIRE(!client.sendCommand({"loadfile", "/tmp/pattern.png", "replace"}).has_value());
}
