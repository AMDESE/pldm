#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pldm::rde
{

/**
 * @struct ResourceInfoView
 * @brief Holds parsed information from a Redfish resource PDR.
 */
struct ResourceInfoView
{
    uint32_t resourceID;
    std::string propContainerName;
    std::string subURI;
    std::string schemaName;
    std::string schemaVersion;
    std::vector<std::string> oemNames;
};

// Constants used for parsing offsets
inline constexpr size_t RESOURCE_FLAGS_SIZE = 1;
inline constexpr size_t CONTAINER_RESOURCE_ID_SIZE = 4;
inline constexpr size_t ADD_RESR_ID_COUNT_SIZE = 2;
inline constexpr size_t SCHEMA_DICT_LENGTH_SIZE = 2;
inline constexpr size_t SCHEMA_DICT_SIGNATURE_SIZE = 4;
inline constexpr int SHIFT_BYTE_1 = 8;
inline constexpr int SHIFT_BYTE_2 = 16;
inline constexpr int SHIFT_BYTE_3 = 24;
inline constexpr size_t BYTE_SIZE = 1;
inline constexpr size_t UINT16_SIZE = 2;
inline constexpr size_t UINT32_SIZE = 4;

/**
 * @class RedfishResourcePDRView
 * @brief Parses Redfish PLDM PDR binary data into structured resource
 * information.
 *
 * This class provides methods to parse binary-encoded Redfish resource PDRs and
 * extract structured data such as resource identifiers, schema names, and OEM
 * names.
 */
class RedfishResourcePDRView
{
  public:
    RedfishResourcePDRView() = default;
    RedfishResourcePDRView(const RedfishResourcePDRView&) = default;
    RedfishResourcePDRView(RedfishResourcePDRView&&) = default;
    RedfishResourcePDRView& operator=(const RedfishResourcePDRView&) = default;
    RedfishResourcePDRView& operator=(RedfishResourcePDRView&&) = default;
    RedfishResourcePDRView(const uint8_t* buffer, size_t length);

    ~RedfishResourcePDRView() = default;

    /**
     * @brief Parses the binary data and populates the ResourceInfoView
     * structure.
     * @param outView Reference to the output structure to populate.
     * @return True if parsing is successful, false otherwise.
     */
    bool parse(struct ResourceInfoView& outView);

  private:
    const uint8_t* cursor = nullptr;
    const uint8_t* end = nullptr;

    bool readByte(uint8_t& val);
    bool readUint16(uint16_t& val);
    bool readUint32(uint32_t& val);
    bool readString(std::string& out);
    void skip(size_t bytes);
};

} // namespace pldm::rde
