// Implements read-only libdrm inspection of max bpc and HDR_OUTPUT_METADATA.

#include "hdrcal/kms.hpp"

#include "hdrcal/unique_fd.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <drm_mode.h>
#include <fcntl.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <xf86drm.h>
#include <xf86drmMode.h>

namespace hdrcal {
namespace {

struct DrmResourcesDeleter {
    void operator()(drmModeRes* resources) const noexcept {
        if (resources != nullptr)
            drmModeFreeResources(resources);
    }
};

struct DrmConnectorDeleter {
    void operator()(drmModeConnector* connector) const noexcept {
        if (connector != nullptr)
            drmModeFreeConnector(connector);
    }
};

struct DrmPropertiesDeleter {
    void operator()(drmModeObjectProperties* properties) const noexcept {
        if (properties != nullptr)
            drmModeFreeObjectProperties(properties);
    }
};

struct DrmPropertyDeleter {
    void operator()(drmModePropertyRes* property) const noexcept {
        if (property != nullptr)
            drmModeFreeProperty(property);
    }
};

struct DrmBlobDeleter {
    void operator()(drmModePropertyBlobRes* blob) const noexcept {
        if (blob != nullptr)
            drmModeFreePropertyBlob(blob);
    }
};

using DrmResourcesPtr = std::unique_ptr<drmModeRes, DrmResourcesDeleter>;
using DrmConnectorPtr = std::unique_ptr<drmModeConnector, DrmConnectorDeleter>;
using DrmPropertiesPtr = std::unique_ptr<drmModeObjectProperties, DrmPropertiesDeleter>;
using DrmPropertyPtr = std::unique_ptr<drmModePropertyRes, DrmPropertyDeleter>;
using DrmBlobPtr = std::unique_ptr<drmModePropertyBlobRes, DrmBlobDeleter>;

struct DrmCardPathList {
    std::vector<std::string> cardPaths;
    std::optional<std::string> error;
};

DrmCardPathList collectDrmCardPaths() {
    DrmCardPathList result;
    std::error_code filesystemError;

    std::filesystem::directory_iterator iterator("/dev/dri", filesystemError);
    if (filesystemError) {
        result.error = "não foi possível acessar /dev/dri: " + filesystemError.message();
        return result;
    }

    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        const std::string fileName = iterator->path().filename().string();
        if (fileName.starts_with("card"))
            result.cardPaths.push_back(iterator->path().string());

        iterator.increment(filesystemError);
        if (filesystemError) {
            result.error = "não foi possível percorrer /dev/dri: " + filesystemError.message();
            return result;
        }
    }

    std::sort(result.cardPaths.begin(), result.cardPaths.end());
    return result;
}

} // namespace

std::string makeConnectorName(std::uint32_t connectorType, std::uint32_t connectorTypeId) {
    const std::string suffix = "-" + std::to_string(connectorTypeId);

    switch (connectorType) {
    case DRM_MODE_CONNECTOR_VGA:
        return "VGA" + suffix;
    case DRM_MODE_CONNECTOR_DVII:
        return "DVI-I" + suffix;
    case DRM_MODE_CONNECTOR_DVID:
        return "DVI-D" + suffix;
    case DRM_MODE_CONNECTOR_DVIA:
        return "DVI-A" + suffix;
    case DRM_MODE_CONNECTOR_Composite:
        return "Composite" + suffix;
    case DRM_MODE_CONNECTOR_SVIDEO:
        return "SVIDEO" + suffix;
    case DRM_MODE_CONNECTOR_LVDS:
        return "LVDS" + suffix;
    case DRM_MODE_CONNECTOR_Component:
        return "Component" + suffix;
    case DRM_MODE_CONNECTOR_9PinDIN:
        return "DIN" + suffix;
    case DRM_MODE_CONNECTOR_DisplayPort:
        return "DP" + suffix;
    case DRM_MODE_CONNECTOR_HDMIA:
        return "HDMI-A" + suffix;
    case DRM_MODE_CONNECTOR_HDMIB:
        return "HDMI-B" + suffix;
    case DRM_MODE_CONNECTOR_TV:
        return "TV" + suffix;
    case DRM_MODE_CONNECTOR_eDP:
        return "eDP" + suffix;
    case DRM_MODE_CONNECTOR_VIRTUAL:
        return "Virtual" + suffix;
    case DRM_MODE_CONNECTOR_DSI:
        return "DSI" + suffix;
    case DRM_MODE_CONNECTOR_DPI:
        return "DPI" + suffix;
    case DRM_MODE_CONNECTOR_WRITEBACK:
        return "Writeback" + suffix;
    case DRM_MODE_CONNECTOR_SPI:
        return "SPI" + suffix;
    case DRM_MODE_CONNECTOR_USB:
        return "USB" + suffix;
    default:
        return "Unknown" + suffix;
    }
}

KmsHdrMetadataResult parseHdrOutputMetadataBlob(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr)
        return {std::nullopt, "blob HDR_OUTPUT_METADATA nulo"};
    if (size < sizeof(hdr_output_metadata))
        return {std::nullopt, "blob HDR_OUTPUT_METADATA menor que a estrutura esperada"};

    hdr_output_metadata metadata{};
    std::memcpy(&metadata, data, sizeof metadata);

    return {KmsHdrMetadata{.eotf = metadata.hdmi_metadata_type1.eotf,
                           .maximumContentLightLevel = metadata.hdmi_metadata_type1.max_cll,
                           .maximumFrameAverageLightLevel = metadata.hdmi_metadata_type1.max_fall},
            std::nullopt};
}

KmsReadResult readKmsConnectorState(const std::string& outputName) {
    const DrmCardPathList cardPathList = collectDrmCardPaths();
    if (cardPathList.error.has_value())
        return {std::nullopt, *cardPathList.error};
    if (cardPathList.cardPaths.empty())
        return {std::nullopt, "nenhum dispositivo /dev/dri/card* acessível"};

    for (const std::string& cardPath : cardPathList.cardPaths) {
        UniqueFileDescriptor device(::open(cardPath.c_str(), O_RDONLY | O_CLOEXEC));
        if (device.get() < 0)
            continue;

        DrmResourcesPtr resources(drmModeGetResources(device.get()));
        if (!resources)
            continue;

        for (int connectorIndex = 0; connectorIndex < resources->count_connectors; ++connectorIndex) {
            DrmConnectorPtr connector(drmModeGetConnectorCurrent(device.get(), resources->connectors[connectorIndex]));
            if (!connector || connector->connection != DRM_MODE_CONNECTED)
                continue;

            const std::string connectorName = makeConnectorName(connector->connector_type, connector->connector_type_id);
            if (connectorName != outputName)
                continue;

            KmsConnectorState state;
            state.connectorName = connectorName;

            DrmPropertiesPtr properties(drmModeObjectGetProperties(device.get(), connector->connector_id, DRM_MODE_OBJECT_CONNECTOR));
            if (!properties)
                return {std::nullopt, "não foi possível ler as propriedades KMS do conector " + connectorName};

            for (std::uint32_t propertyIndex = 0; propertyIndex < properties->count_props; ++propertyIndex) {
                DrmPropertyPtr property(drmModeGetProperty(device.get(), properties->props[propertyIndex]));
                if (!property)
                    continue;

                const std::string propertyName = property->name;
                const std::uint64_t propertyValue = properties->prop_values[propertyIndex];

                if (propertyName == "max bpc") {
                    state.maximumBitsPerComponent = static_cast<int>(propertyValue);
                    continue;
                }

                if (propertyName != "HDR_OUTPUT_METADATA" || propertyValue == 0)
                    continue;

                DrmBlobPtr blob(drmModeGetPropertyBlob(device.get(), propertyValue));
                if (!blob)
                    continue;

                const KmsHdrMetadataResult parsed = parseHdrOutputMetadataBlob(static_cast<const std::uint8_t*>(blob->data), blob->length);
                if (!parsed.value)
                    continue;

                state.hdrEotf = parsed.value->eotf;
                state.maximumContentLightLevel = parsed.value->maximumContentLightLevel;
                state.maximumFrameAverageLightLevel = parsed.value->maximumFrameAverageLightLevel;
            }

            return {state, std::nullopt};
        }
    }

    return {std::nullopt, "conector KMS ativo '" + outputName + "' não encontrado em /dev/dri/card*"};
}

} // namespace hdrcal
