// Read-only KMS connector inspection for the HDR preconditions.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace hdrcal {

// HDR metadata decoded from the KMS connector property HDR_OUTPUT_METADATA.
// The eotf follows the CTA-861 values; 2 means SMPTE ST 2084 (PQ).
struct KmsHdrMetadata {
    int eotf = 0;
    unsigned int maximumContentLightLevel = 0;
    unsigned int maximumFrameAverageLightLevel = 0;
};

struct KmsHdrMetadataResult {
    std::optional<KmsHdrMetadata> value;
    std::optional<std::string> error;
};

// State needed to verify the HDR preconditions of section 8.2 of the project
// instructions.
struct KmsConnectorState {
    std::string connectorName;
    int maximumBitsPerComponent = 0;
    std::optional<int> hdrEotf;
    unsigned int maximumContentLightLevel = 0;
    unsigned int maximumFrameAverageLightLevel = 0;
};

struct KmsReadResult {
    std::optional<KmsConnectorState> value;
    std::optional<std::string> error;
};

// Builds the Hyprland-style connector name from the DRM connector type and
// type id, for example (DisplayPort, 2) -> "DP-2".
[[nodiscard]] std::string makeConnectorName(std::uint32_t connectorType, std::uint32_t connectorTypeId);

// Decodes a raw HDR_OUTPUT_METADATA blob. The input is copied into the kernel
// struct, so unaligned buffers are safe.
[[nodiscard]] KmsHdrMetadataResult parseHdrOutputMetadataBlob(const std::uint8_t* data, std::size_t size);

// Reads the live KMS connector state from /dev/dri/card*. Read-only; no DRM
// master or root privileges are required when the user is in the video group.
[[nodiscard]] KmsReadResult readKmsConnectorState(const std::string& outputName);

// Injection point for tests, so precondition checks never need a GPU.
using KmsConnectorReader = std::function<KmsReadResult(const std::string& outputName)>;

} // namespace hdrcal
