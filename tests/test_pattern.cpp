// Unit tests for 16-bit PQ pattern generation and PNG colour tagging.

#include "hdrcal/pattern.hpp"
#include "test_framework.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>

namespace {

struct DecodedPng {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t bitDepth = 0;
    std::uint8_t colourType = 0;
    std::vector<std::uint8_t> cicp;
    std::vector<std::uint8_t> rawPixels;
};

std::uint32_t readUint32BigEndian(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) | static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::uint16_t readUint16BigEndian(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8) | bytes[offset + 1]);
}

DecodedPng decodePng(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    HDRCAL_REQUIRE(input.is_open());
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    HDRCAL_REQUIRE(bytes.size() > 8);
    HDRCAL_REQUIRE(std::memcmp(bytes.data(), "\x89PNG\r\n\x1a\n", 8) == 0);

    DecodedPng decoded;
    std::string compressed;

    std::size_t offset = 8;
    while (offset + 12 <= bytes.size()) {
        const std::uint32_t length = readUint32BigEndian(bytes, offset);
        const std::string chunkType(reinterpret_cast<const char*>(bytes.data() + offset + 4), 4);
        const std::size_t payloadOffset = offset + 8;
        HDRCAL_REQUIRE(payloadOffset + length + 4 <= bytes.size());

        if (chunkType == "IHDR") {
            decoded.width = readUint32BigEndian(bytes, payloadOffset);
            decoded.height = readUint32BigEndian(bytes, payloadOffset + 4);
            decoded.bitDepth = bytes.at(payloadOffset + 8);
            decoded.colourType = bytes.at(payloadOffset + 9);
        } else if (chunkType == "cICP") {
            decoded.cicp.assign(bytes.begin() + static_cast<std::ptrdiff_t>(payloadOffset),
                                bytes.begin() + static_cast<std::ptrdiff_t>(payloadOffset + length));
        } else if (chunkType == "IDAT") {
            compressed.append(reinterpret_cast<const char*>(bytes.data() + payloadOffset), length);
        }

        offset = payloadOffset + length + 4;
    }

    const std::size_t expectedRawSize = static_cast<std::size_t>(decoded.height) * (1 + static_cast<std::size_t>(decoded.width) * 6);
    decoded.rawPixels.resize(expectedRawSize);

    uLongf rawSize = static_cast<uLongf>(expectedRawSize);
    HDRCAL_REQUIRE_EQ(uncompress(decoded.rawPixels.data(), &rawSize, reinterpret_cast<const Bytef*>(compressed.data()), static_cast<uLong>(compressed.size())),
                      Z_OK);
    HDRCAL_REQUIRE_EQ(rawSize, static_cast<uLongf>(expectedRawSize));

    return decoded;
}

std::uint16_t redSampleAt(const DecodedPng& png, int x, int y) {
    const std::size_t rowBytes = 1 + static_cast<std::size_t>(png.width) * 6;
    const std::size_t offset = static_cast<std::size_t>(y) * rowBytes + 1 + static_cast<std::size_t>(x) * 6;
    return readUint16BigEndian(png.rawPixels, offset);
}

} // namespace

HDRCAL_TEST(testComputeSixteenBitSampleFromTenBitCode_KnownValuesAndClamp) {
    HDRCAL_REQUIRE_EQ(hdrcal::computeSixteenBitSampleFromTenBitCode(0), std::uint16_t{0});
    HDRCAL_REQUIRE_EQ(hdrcal::computeSixteenBitSampleFromTenBitCode(653), std::uint16_t{41832});
    HDRCAL_REQUIRE_EQ(hdrcal::computeSixteenBitSampleFromTenBitCode(712), std::uint16_t{45612});
    HDRCAL_REQUIRE_EQ(hdrcal::computeSixteenBitSampleFromTenBitCode(1023), std::uint16_t{65535});
    HDRCAL_REQUIRE_EQ(hdrcal::computeSixteenBitSampleFromTenBitCode(2000), std::uint16_t{65535});
}

HDRCAL_TEST(testWritePqPatternPng_MinimumPhase_HasHeaderCicpAndCentredPatch) {
    const hdrcal::test::TemporaryDirectory temporaryDirectory("hdrcal-pattern-minimum");
    const std::filesystem::path destination = temporaryDirectory.path() / "pattern.png";

    HDRCAL_REQUIRE(!hdrcal::writePqPatternPng(
                        hdrcal::PatternSpec{.phase = hdrcal::PatternPhase::MinimumLuminance, .tenBitCode = 653, .width = 100, .height = 80}, destination)
                        .has_value());

    const DecodedPng png = decodePng(destination);
    HDRCAL_REQUIRE_EQ(png.width, std::uint32_t{100});
    HDRCAL_REQUIRE_EQ(png.height, std::uint32_t{80});
    HDRCAL_REQUIRE_EQ(png.bitDepth, std::uint8_t{16});
    HDRCAL_REQUIRE_EQ(png.colourType, std::uint8_t{2});
    HDRCAL_REQUIRE_EQ(png.cicp.size(), std::size_t{4});
    HDRCAL_REQUIRE_EQ(png.cicp.at(0), std::uint8_t{9});
    HDRCAL_REQUIRE_EQ(png.cicp.at(1), std::uint8_t{16});
    HDRCAL_REQUIRE_EQ(png.cicp.at(2), std::uint8_t{0});
    HDRCAL_REQUIRE_EQ(png.cicp.at(3), std::uint8_t{1});

    // Patch is 32x32 centred at (34, 24) for a 100x80 image.
    HDRCAL_REQUIRE_EQ(redSampleAt(png, 0, 0), std::uint16_t{0});
    HDRCAL_REQUIRE_EQ(redSampleAt(png, 34, 24), std::uint16_t{41832});
    HDRCAL_REQUIRE_EQ(redSampleAt(png, 65, 55), std::uint16_t{41832});
    HDRCAL_REQUIRE_EQ(redSampleAt(png, 66, 56), std::uint16_t{0});
}

HDRCAL_TEST(testWritePqPatternPng_MaximumPhase_UsesCheckerboardInsidePatch) {
    const hdrcal::test::TemporaryDirectory temporaryDirectory("hdrcal-pattern-maximum");
    const std::filesystem::path destination = temporaryDirectory.path() / "pattern.png";

    HDRCAL_REQUIRE(!hdrcal::writePqPatternPng(
                        hdrcal::PatternSpec{.phase = hdrcal::PatternPhase::MaximumLuminance, .tenBitCode = 712, .width = 100, .height = 80}, destination)
                        .has_value());

    const DecodedPng png = decodePng(destination);
    const std::uint16_t highSample = hdrcal::computeSixteenBitSampleFromTenBitCode(712);
    const std::uint16_t lowSample = hdrcal::computeSixteenBitSampleFromTenBitCode(696);

    HDRCAL_REQUIRE_EQ(redSampleAt(png, 34, 24), highSample);
    HDRCAL_REQUIRE_EQ(redSampleAt(png, 50, 24), lowSample);
    HDRCAL_REQUIRE_EQ(redSampleAt(png, 34, 40), lowSample);
    HDRCAL_REQUIRE_EQ(redSampleAt(png, 50, 40), highSample);
    HDRCAL_REQUIRE_EQ(redSampleAt(png, 10, 10), std::uint16_t{0});
}

HDRCAL_TEST(testWritePqPatternPng_FullFramePhase_IsUniform) {
    const hdrcal::test::TemporaryDirectory temporaryDirectory("hdrcal-pattern-full-frame");
    const std::filesystem::path destination = temporaryDirectory.path() / "pattern.png";

    HDRCAL_REQUIRE(
        !hdrcal::writePqPatternPng(hdrcal::PatternSpec{.phase = hdrcal::PatternPhase::MaximumFullFrameLuminance, .tenBitCode = 592, .width = 64, .height = 48},
                                   destination)
             .has_value());

    const DecodedPng png = decodePng(destination);
    const std::uint16_t expected = hdrcal::computeSixteenBitSampleFromTenBitCode(592);

    HDRCAL_REQUIRE_EQ(redSampleAt(png, 0, 0), expected);
    HDRCAL_REQUIRE_EQ(redSampleAt(png, 63, 47), expected);
    HDRCAL_REQUIRE_EQ(redSampleAt(png, 32, 24), expected);
}

HDRCAL_TEST(testWritePqPatternPng_InvalidSpec_ReturnsErrorAndCreatesNothing) {
    const hdrcal::test::TemporaryDirectory temporaryDirectory("hdrcal-pattern-invalid");
    const std::filesystem::path destination = temporaryDirectory.path() / "pattern.png";

    HDRCAL_REQUIRE(hdrcal::writePqPatternPng(hdrcal::PatternSpec{.tenBitCode = 1024, .width = 64, .height = 48}, destination).has_value());
    HDRCAL_REQUIRE(!std::filesystem::exists(destination));

    HDRCAL_REQUIRE(hdrcal::writePqPatternPng(hdrcal::PatternSpec{.tenBitCode = 10, .width = 0, .height = 48}, destination).has_value());
    HDRCAL_REQUIRE(!std::filesystem::exists(destination));
}

HDRCAL_TEST(testPatternPhaseName_ReturnsStableIdentifiers) {
    HDRCAL_REQUIRE_EQ(hdrcal::patternPhaseName(hdrcal::PatternPhase::MinimumLuminance), std::string("minimum"));
    HDRCAL_REQUIRE_EQ(hdrcal::patternPhaseName(hdrcal::PatternPhase::MaximumLuminance), std::string("maximum"));
    HDRCAL_REQUIRE_EQ(hdrcal::patternPhaseName(hdrcal::PatternPhase::MaximumFullFrameLuminance), std::string("full-frame"));
}
