// Unit tests for connector naming and HDR metadata blob decoding.

#include "hdrcal/kms.hpp"
#include "test_framework.hpp"

#include <cstdint>
#include <cstring>
#include <drm_mode.h>

HDRCAL_TEST(testMakeConnectorName_MapsDisplayPortHdmiAndEdp) {
    HDRCAL_REQUIRE_EQ(hdrcal::makeConnectorName(DRM_MODE_CONNECTOR_DisplayPort, 2), std::string("DP-2"));
    HDRCAL_REQUIRE_EQ(hdrcal::makeConnectorName(DRM_MODE_CONNECTOR_HDMIA, 1), std::string("HDMI-A-1"));
    HDRCAL_REQUIRE_EQ(hdrcal::makeConnectorName(DRM_MODE_CONNECTOR_HDMIB, 3), std::string("HDMI-B-3"));
    HDRCAL_REQUIRE_EQ(hdrcal::makeConnectorName(DRM_MODE_CONNECTOR_eDP, 1), std::string("eDP-1"));
    HDRCAL_REQUIRE_EQ(hdrcal::makeConnectorName(DRM_MODE_CONNECTOR_Unknown, 7), std::string("Unknown-7"));
}

HDRCAL_TEST(testParseHdrOutputMetadataBlob_ReadsEotfAndLightLevels) {
    hdr_output_metadata metadata{};
    metadata.metadata_type = 0;
    metadata.hdmi_metadata_type1.eotf = 2;
    metadata.hdmi_metadata_type1.max_cll = 600;
    metadata.hdmi_metadata_type1.max_fall = 350;

    const hdrcal::KmsHdrMetadataResult result = hdrcal::parseHdrOutputMetadataBlob(reinterpret_cast<const std::uint8_t*>(&metadata), sizeof metadata);

    HDRCAL_REQUIRE(result.value.has_value());
    HDRCAL_REQUIRE_EQ(result.value->eotf, 2);
    HDRCAL_REQUIRE_EQ(result.value->maximumContentLightLevel, 600U);
    HDRCAL_REQUIRE_EQ(result.value->maximumFrameAverageLightLevel, 350U);
}

HDRCAL_TEST(testParseHdrOutputMetadataBlob_TooShort_ReturnsError) {
    const std::uint8_t truncated[] = {0, 0, 0};
    const hdrcal::KmsHdrMetadataResult result = hdrcal::parseHdrOutputMetadataBlob(truncated, sizeof truncated);
    HDRCAL_REQUIRE(!result.value.has_value());
    HDRCAL_REQUIRE(result.error.has_value());
}

HDRCAL_TEST(testParseHdrOutputMetadataBlob_Null_ReturnsError) {
    const hdrcal::KmsHdrMetadataResult result = hdrcal::parseHdrOutputMetadataBlob(nullptr, 0);
    HDRCAL_REQUIRE(!result.value.has_value());
    HDRCAL_REQUIRE(result.error.has_value());
}

HDRCAL_TEST(testSelectKmsCardForConnector_UniqueMatch_ReturnsCardPath) {
    const std::vector<hdrcal::KmsCardConnectorList> cards = {
        {.cardPath = "/dev/dri/card0", .connectedConnectorNames = {"eDP-1"}},
        {.cardPath = "/dev/dri/card1", .connectedConnectorNames = {"DP-2", "HDMI-A-1"}},
    };

    const hdrcal::KmsCardSelectionResult result = hdrcal::selectKmsCardForConnector(cards, "DP-2");
    HDRCAL_REQUIRE(result.cardPath.has_value());
    HDRCAL_REQUIRE_EQ(*result.cardPath, std::string("/dev/dri/card1"));
    HDRCAL_REQUIRE(!result.error.has_value());
}

HDRCAL_TEST(testSelectKmsCardForConnector_NoMatch_ReturnsError) {
    const std::vector<hdrcal::KmsCardConnectorList> cards = {
        {.cardPath = "/dev/dri/card0", .connectedConnectorNames = {"eDP-1"}},
    };

    const hdrcal::KmsCardSelectionResult result = hdrcal::selectKmsCardForConnector(cards, "DP-2");
    HDRCAL_REQUIRE(!result.cardPath.has_value());
    HDRCAL_REQUIRE(result.error.has_value());
    HDRCAL_REQUIRE(result.error->find("DP-2") != std::string::npos);
}

HDRCAL_TEST(testSelectKmsCardForConnector_AmbiguousName_ReportsEveryCardPath) {
    const std::vector<hdrcal::KmsCardConnectorList> cards = {
        {.cardPath = "/dev/dri/card0", .connectedConnectorNames = {"DP-2"}},
        {.cardPath = "/dev/dri/card1", .connectedConnectorNames = {"DP-2"}},
    };

    const hdrcal::KmsCardSelectionResult result = hdrcal::selectKmsCardForConnector(cards, "DP-2");
    HDRCAL_REQUIRE(!result.cardPath.has_value());
    HDRCAL_REQUIRE(result.error.has_value());
    HDRCAL_REQUIRE(result.error->find("mais de um card") != std::string::npos);
    HDRCAL_REQUIRE(result.error->find("/dev/dri/card0") != std::string::npos);
    HDRCAL_REQUIRE(result.error->find("/dev/dri/card1") != std::string::npos);
}
