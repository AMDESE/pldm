#pragma once

#include "rde_cache_manager.hpp"
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/RDE/CacheManager/server.hpp>

#include <vector>
#include <string>

namespace pldm::rde
{

using CacheManagerIface = sdbusplus::server::object_t<
    sdbusplus::xyz::openbmc_project::RDE::server::CacheManager>;

class CacheManagerObject : public CacheManagerIface
{
  public:
    CacheManagerObject(sdbusplus::bus_t& bus, const char* path) :
        CacheManagerIface(bus, path)
    {}

    /**
     * @brief Implementation for GetCurrentCache
     * Retrieve all cached operations for a specific RDE device.
     *
     * @param[in] uuid - The UUID of the device to query.
     * @return Operations - List of cached operations as JSON strings.
     */
    std::vector<std::string> getCurrentCache(std::string uuid) override;

    /**
     * @brief Implementation for CreateCache
     * Manually create a cache entry for a specific RDE device.
     * Parameter order matches StartRedfishOperation (excluding OperationID and EID).
     *
     * @param[in] operationType - Operation type
     * @param[in] targetURI - Target URI
     * @param[in] deviceUUID - Device UUID
     * @param[in] payload - Operation payload (JSON string)
     * @param[in] payloadFormat - Payload format (default: Inline)
     * @param[in] encodingFormat - Encoding format (default: JSON)
     * @param[in] sessionId - Session ID (optional)
     * @return bool - true if successfully cached, false otherwise
     */
    bool createCache(
        sdbusplus::common::xyz::openbmc_project::rde::Common::OperationType
            operationType,
        std::string targetURI, std::string deviceUUID,
        std::string payload,
        sdbusplus::xyz::openbmc_project::RDE::server::Manager::PayloadFormatType
            payloadFormat,
        sdbusplus::xyz::openbmc_project::RDE::server::Manager::EncodingFormatType
            encodingFormat,
        std::string sessionId) override;
};

} // namespace pldm::rde

