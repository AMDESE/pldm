#include "rde/redfish_resource_pdr_view.hpp"

#include <libpldm/platform.h>

#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace pldm::rde;

// Constants for mock data
constexpr uint32_t kResourceID = 1;
constexpr uint8_t kResourceFlags = 0x00;
constexpr uint32_t kContainerResourceID = 0;
const std::string kPropContainerName = "Contai";
const std::string kSubURI = "subURI";
const std::string kSchemaName = "Schema";
constexpr uint8_t kSchemaMajor = 1;
constexpr uint8_t kSchemaMinor = 0;
constexpr uint8_t kSchemaUpdate = 0;
constexpr uint8_t kSchemaAlpha = 0;
constexpr uint16_t kSchemaDictLength = 0;
constexpr uint32_t kSchemaDictSignature = 0;
constexpr uint16_t kOEMCount = 2;
const std::vector<std::string> kOEMNames = {"OEM1", "OEM2"};

// Helper to encode a string with a 2-byte length prefix
void encodeString(std::vector<uint8_t>& buffer, const std::string& str)
{
    uint16_t len = static_cast<uint16_t>(str.size());
    buffer.push_back(static_cast<uint8_t>(len & 0xFF));
    buffer.push_back(static_cast<uint8_t>((len >> SHIFT_BYTE_1) & 0xFF));
    buffer.insert(buffer.end(), str.begin(), str.end());
}

// Build a mock Redfish Resource PDR payload
std::vector<uint8_t> buildMockRedfishResourcePDR()
{
    std::vector<uint8_t> buffer;

    // Resource ID
    buffer.push_back(static_cast<uint8_t>(kResourceID & 0xFF));
    buffer.push_back(
        static_cast<uint8_t>((kResourceID >> SHIFT_BYTE_1) & 0xFF));
    buffer.push_back(
        static_cast<uint8_t>((kResourceID >> SHIFT_BYTE_2) & 0xFF));
    buffer.push_back(
        static_cast<uint8_t>((kResourceID >> SHIFT_BYTE_3) & 0xFF));

    // Resource Flags
    buffer.push_back(kResourceFlags);

    // Container Resource ID
    buffer.push_back(static_cast<uint8_t>(kContainerResourceID & 0xFF));
    buffer.push_back(
        static_cast<uint8_t>((kContainerResourceID >> SHIFT_BYTE_1) & 0xFF));
    buffer.push_back(
        static_cast<uint8_t>((kContainerResourceID >> SHIFT_BYTE_2) & 0xFF));
    buffer.push_back(
        static_cast<uint8_t>((kContainerResourceID >> SHIFT_BYTE_3) & 0xFF));

    // Property Container Name
    encodeString(buffer, kPropContainerName);

    // SubURI
    encodeString(buffer, kSubURI);

    // Add Resource ID Count (2 bytes, skipped)
    buffer.push_back(0x00);
    buffer.push_back(0x00);

    // Schema Version
    buffer.push_back(kSchemaMajor);
    buffer.push_back(kSchemaMinor);
    buffer.push_back(kSchemaUpdate);
    buffer.push_back(kSchemaAlpha);

    // Schema Dict Length (2 bytes, skipped)
    buffer.push_back(static_cast<uint8_t>(kSchemaDictLength & 0xFF));
    buffer.push_back(
        static_cast<uint8_t>((kSchemaDictLength >> SHIFT_BYTE_1) & 0xFF));

    // Schema Dict Signature (4 bytes, skipped)
    buffer.push_back(static_cast<uint8_t>(kSchemaDictSignature & 0xFF));
    buffer.push_back(
        static_cast<uint8_t>((kSchemaDictSignature >> SHIFT_BYTE_1) & 0xFF));
    buffer.push_back(
        static_cast<uint8_t>((kSchemaDictSignature >> SHIFT_BYTE_2) & 0xFF));
    buffer.push_back(
        static_cast<uint8_t>((kSchemaDictSignature >> SHIFT_BYTE_3) & 0xFF));

    // Schema Name
    encodeString(buffer, kSchemaName);

    // OEM Count
    buffer.push_back(static_cast<uint8_t>(kOEMCount & 0xFF));
    buffer.push_back(static_cast<uint8_t>((kOEMCount >> SHIFT_BYTE_1) & 0xFF));

    // OEM Names
    for (const auto& oem : kOEMNames)
    {
        encodeString(buffer, oem);
    }

    return buffer;
}

TEST(RedfishResourcePDRViewTest, ParseValidPayload)
{
    auto payload = buildMockRedfishResourcePDR();
    ResourceInfoView view;
    RedfishResourcePDRView parser(payload.data(), payload.size());

    ASSERT_TRUE(parser.parse(view));
    EXPECT_EQ(view.resourceID, kResourceID);
    EXPECT_EQ(view.propContainerName, kPropContainerName);
    EXPECT_EQ(view.subURI, kSubURI);
    EXPECT_EQ(view.schemaName, kSchemaName);
    EXPECT_EQ(view.schemaVersion, "1.0.0.0");
    ASSERT_EQ(view.oemNames.size(), kOEMNames.size());
    EXPECT_EQ(view.oemNames[0], kOEMNames[0]);
    EXPECT_EQ(view.oemNames[1], kOEMNames[1]);
}
