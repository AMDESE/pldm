#include "utils.hpp"

#ifdef OEM_AMD
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#endif

namespace pldm::rde
{
void logCompletionCodeError(uint8_t cc)
{
    std::string message;

    switch (cc)
    {
        case PLDM_RDE_BAD_CHECKSUM:
            message = "BAD_CHECKSUM: The payload checksum is incorrect";
            break;
        case PLDM_RDE_CANNOT_CREATE_OPERATION:
            message = "CANNOT_CREATE_OPERATION: Unable to create operation";
            break;
        case PLDM_RDE_NOT_ALLOWED:
            message = "NOT_ALLOWED: Operation not permitted";
            break;
        case PLDM_RDE_WRONG_LOCATION_TYPE:
            message = "WRONG_LOCATION_TYPE: Invalid location type specified";
            break;
        case PLDM_RDE_ERROR_OPERATION_ABANDONED:
            message = "OPERATION_ABANDONED: Operation was aborted unexpectedly";
            break;
        case PLDM_RDE_OPERATION_UNKILLABLE:
            message = "OPERATION_UNKILLABLE: Cannot forcibly cancel operation";
            break;
        case PLDM_RDE_ERROR_OPERATION_EXISTS:
            message = "OPERATION_EXISTS: Duplicate operation detected";
            break;
        case PLDM_RDE_ERROR_OPERATION_FAILED:
            message = "OPERATION_FAILED: Operation execution failed";
            break;
        case PLDM_RDE_ERROR_UNEXPECTED:
            message = "UNEXPECTED_ERROR: Internal or unknown error occurred";
            break;
        case PLDM_RDE_ERROR_UNSUPPORTED:
            message = "UNSUPPORTED: Command or resource not supported";
            break;
        case PLDM_RDE_ERROR_UNRECOGNIZED_CUSTOM_HEADER:
            message =
                "UNRECOGNIZED_CUSTOM_HEADER: Header format not recognized";
            break;
        case PLDM_RDE_ERROR_ETAG_MATCH:
            message = "ETAG_MATCH_FAILED: ETag comparison mismatch";
            break;
        case PLDM_RDE_ERROR_NO_SUCH_RESOURCE:
            message = "NO_SUCH_RESOURCE: Referenced resource was not found";
            break;
        case PLDM_RDE_ERROR_ETAG_CALCULATION_ONGOING:
            message = "ETAG_CALCULATION_ONGOING: ETag generation in progress";
            break;
        default:
            message = "Unknown CompletionCode: cc=" + std::to_string(cc);
            break;
    }

    error("handleOperationInitResp failed with CompletionCode {CC}: {MSG}",
          "CC", cc, "MSG", message);
}

void logHexPayload(const std::vector<uint8_t>& payload)
{
    std::ostringstream oss;
    for (uint8_t byte : payload)
    {
        oss << std::hex << std::uppercase << std::setfill('0') << std::setw(2)
            << static_cast<int>(byte) << " ";
    }
    lg2::info("Payload HEX dump: {BYTES}", "BYTES", oss.str());
}

#ifdef OEM_AMD
std::string loadProcessorURI(const std::string& devUUID)
{
    constexpr const char* rdeDeviceMetadataFile =
        "/etc/pldm/rde_device_metadata.json";

    if (!std::filesystem::exists(rdeDeviceMetadataFile))
    {
        error("RDE: Device metadata file {FILE} not found: ", "FILE",
              rdeDeviceMetadataFile);
        return "";
    }

    std::ifstream file(rdeDeviceMetadataFile);
    if (!file.is_open())
    {
        error("RDE: Failed to open device metadata file:{FILE} ", "FILE",
              rdeDeviceMetadataFile);
        return "";
    }

    try
    {
        if (file.peek() == std::ifstream::traits_type::eof())
        {
            error("RDE: Device metadata file{FILE} is empty: ", "FILE",
                  rdeDeviceMetadataFile);
            return "";
        }

        nlohmann::json jsonData;
        file >> jsonData;

        if (!jsonData.is_object())
        {
            error(
                "RDE: Device metadata file does not contain a valid JSON object.");
            return "";
        }

        for (const auto& [jsonSchema, deviceEntries] : jsonData.items())
        {
            if (!deviceEntries.is_object())
            {
                error("RDE: Invalid schema section {SCHEMA} ", "SCHEMA",
                      jsonSchema);
                continue;
            }

            for (const auto& [jsonDeviceId, deviceInfo] : deviceEntries.items())
            {
                if (!deviceInfo.contains("UUIDs") ||
                    !deviceInfo["UUIDs"].is_array())
                {
                    error(
                        "RDE: Missing or invalid 'UUIDs' for {SCHEMA} {DEVID}",
                        "SCHEMA", jsonSchema, "DEVID", jsonDeviceId);
                    continue;
                }

                const std::string deviceKeyFromJson =
                    jsonSchema + "/" + jsonDeviceId + "/";

                for (const auto& uuid : deviceInfo["UUIDs"])
                {
                    if (!uuid.is_string())
                    {
                        error("RDE: Invalid UUID format in {KEY}", "KEY",
                              deviceKeyFromJson);
                        continue;
                    }
                    if (devUUID == uuid.get<std::string>())
                    {
                        return deviceKeyFromJson;
                    }
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        error("RDE: Unexpected error while reading device metadata file:{MSG} ",
              "MSG", e.what());
        return "";
    }

    return "";
}
#endif

} // namespace pldm::rde
