// Implements 16-bit PQ pattern PNG generation and cICP tagging.

#include "hdrcal/pattern.hpp"

#include "hdrcal/atomic_file.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <zlib.h>

namespace hdrcal {
namespace {

constexpr int kMaximumPatternDimension = 16384;
constexpr int kCheckerboardCellPixels = 16;
constexpr int kMinimumPatchPixels = 32;

void appendUint32BigEndian(std::string& destination, std::uint32_t value) {
    destination.push_back(static_cast<char>((value >> 24) & 0xFF));
    destination.push_back(static_cast<char>((value >> 16) & 0xFF));
    destination.push_back(static_cast<char>((value >> 8) & 0xFF));
    destination.push_back(static_cast<char>(value & 0xFF));
}

void appendPngChunk(std::string& destination, const char* chunkType, const std::string& payload) {
    appendUint32BigEndian(destination, static_cast<std::uint32_t>(payload.size()));

    const std::size_t crcStart = destination.size();
    destination.append(chunkType, 4);
    destination.append(payload);

    const uLong crc = crc32(0L, Z_NULL, 0);
    const uLong updatedCrc = crc32(crc, reinterpret_cast<const Bytef*>(destination.data() + crcStart), static_cast<uInt>(destination.size() - crcStart));
    appendUint32BigEndian(destination, static_cast<std::uint32_t>(updatedCrc));
}

void appendSample(std::string& destination, std::uint16_t sample) {
    destination.push_back(static_cast<char>((sample >> 8) & 0xFF));
    destination.push_back(static_cast<char>(sample & 0xFF));
}

struct PatchLayout {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
};

PatchLayout computePatchLayout(int imageWidth, int imageHeight) {
    const int patchWidth = std::max(kMinimumPatchPixels, imageWidth / 4);
    const int patchHeight = std::max(kMinimumPatchPixels, imageHeight / 4);

    return PatchLayout{
        .left = (imageWidth - patchWidth) / 2,
        .top = (imageHeight - patchHeight) / 2,
        .width = patchWidth,
        .height = patchHeight,
    };
}

bool isInsidePatch(const PatchLayout& patch, int x, int y) {
    return x >= patch.left && x < patch.left + patch.width && y >= patch.top && y < patch.top + patch.height;
}

std::uint16_t sampleAt(const PatternSpec& spec, const PatchLayout& patch, std::uint16_t sample, std::uint16_t lowSample, int x, int y) {
    if (spec.phase == PatternPhase::MaximumFullFrameLuminance)
        return sample;

    if (!isInsidePatch(patch, x, y))
        return 0;

    if (spec.phase != PatternPhase::MaximumLuminance)
        return sample;

    const int cellX = (x - patch.left) / kCheckerboardCellPixels;
    const int cellY = (y - patch.top) / kCheckerboardCellPixels;
    return (cellX + cellY) % 2 == 0 ? sample : lowSample;
}

std::string encodePng(const PatternSpec& spec, std::uint16_t sample, std::uint16_t lowSample) {
    const PatchLayout patch = computePatchLayout(spec.width, spec.height);

    const std::size_t rowBytes = 1 + static_cast<std::size_t>(spec.width) * 6;
    std::string raw;
    raw.reserve(rowBytes * static_cast<std::size_t>(spec.height));

    for (int y = 0; y < spec.height; ++y) {
        raw.push_back('\0'); // filter type 0 (none)

        for (int x = 0; x < spec.width; ++x) {
            const std::uint16_t pixelSample = sampleAt(spec, patch, sample, lowSample, x, y);
            appendSample(raw, pixelSample);
            appendSample(raw, pixelSample);
            appendSample(raw, pixelSample);
        }
    }

    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::string compressed(compressedSize, '\0');
    if (compress2(reinterpret_cast<Bytef*>(compressed.data()), &compressedSize, reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()),
                  Z_BEST_SPEED) != Z_OK) {
        return {};
    }
    compressed.resize(compressedSize);

    std::string png;
    png.reserve(compressed.size() + 128);
    png.append("\x89PNG\r\n\x1a\n", 8);

    std::string header;
    appendUint32BigEndian(header, static_cast<std::uint32_t>(spec.width));
    appendUint32BigEndian(header, static_cast<std::uint32_t>(spec.height));
    header.push_back(16); // bit depth
    header.push_back(2);  // colour type: truecolour RGB
    header.push_back(0);  // compression
    header.push_back(0);  // filter
    header.push_back(0);  // interlace
    appendPngChunk(png, "IHDR", header);

    // BT.2020 primaries, SMPTE ST 2084 transfer, RGB matrix, full range.
    appendPngChunk(png, "cICP", std::string{static_cast<char>(9), static_cast<char>(16), static_cast<char>(0), static_cast<char>(1)});
    appendPngChunk(png, "IDAT", compressed);
    appendPngChunk(png, "IEND", {});

    return png;
}

} // namespace

std::string patternPhaseName(PatternPhase phase) {
    switch (phase) {
    case PatternPhase::MinimumLuminance:
        return "minimum";
    case PatternPhase::MaximumLuminance:
        return "maximum";
    case PatternPhase::MaximumFullFrameLuminance:
        return "full-frame";
    }
    return "unknown";
}

std::uint16_t computeSixteenBitSampleFromTenBitCode(std::uint16_t tenBitCode) {
    const std::uint16_t clamped = std::min<std::uint16_t>(tenBitCode, 1023);
    return static_cast<std::uint16_t>(std::lround(static_cast<double>(clamped) / 1023.0 * 65535.0));
}

std::optional<std::string> writePqPatternPng(const PatternSpec& spec, const std::filesystem::path& destinationPath) {
    if (spec.width <= 0 || spec.height <= 0 || spec.width > kMaximumPatternDimension || spec.height > kMaximumPatternDimension)
        return "dimensões de padrão inválidas";

    if (spec.tenBitCode > 1023)
        return "código PQ inválido; use 0..1023";

    const std::uint16_t sample = computeSixteenBitSampleFromTenBitCode(spec.tenBitCode);
    const std::uint16_t lowSample = computeSixteenBitSampleFromTenBitCode(spec.tenBitCode >= 16 ? spec.tenBitCode - 16 : 0);

    const std::string png = encodePng(spec, sample, lowSample);
    if (png.empty())
        return "não foi possível comprimir o padrão PNG";

    return writeFileAtomically(destinationPath, png);
}

} // namespace hdrcal
