#include "cache_manager_dbus.hpp"
#include "rde/device_common.hpp"
#include "rde/utils.hpp"
#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

PHOSPHOR_LOG2_USING;

using namespace sdbusplus::xyz::openbmc_project::Common::Error;

namespace pldm::rde
{

std::vector<std::string> CacheManagerObject::getCurrentCache(std::string uuid)
{
    std::vector<std::string> results;
    
    // Get internal cache data from singleton
    auto internalCache = RDECacheManager::getInstance().getCache(uuid);
    
    for (const auto& entry : internalCache)
    {
        nlohmann::json j;
        j["Operation"] = sdbusplus::message::convert_to_string(entry.operationType);
        j["URI"] = entry.targetURI;
        j["Payload"] = entry.payload;
        j["PayloadFormat"] = sdbusplus::message::convert_to_string(entry.payloadFormat);
        j["EncodingFormat"] = sdbusplus::message::convert_to_string(entry.encodingType);
        j["SessionId"] = entry.sessionId;
        j["Timestamp"] = entry.timestamp;
        j["Status"] = (entry.status == CacheStatus::Pending) ? "Pending" : "Processing";
        
        results.push_back(j.dump());
    }
    
    return results;
}

bool CacheManagerObject::createCache(
    sdbusplus::common::xyz::openbmc_project::rde::Common::OperationType
        operationType,
    std::string targetURI, std::string deviceUUID,
    std::string payload,
    sdbusplus::xyz::openbmc_project::RDE::server::Manager::PayloadFormatType
        payloadFormat,
    sdbusplus::xyz::openbmc_project::RDE::server::Manager::EncodingFormatType
        encodingFormat,
    std::string sessionId)
{
    info("RDE Cache: createCache called for UUID={UUID}", "UUID", deviceUUID);

    // Validate deviceUUID exists in metadata
    std::string processorURI = loadProcessorURI(deviceUUID);
    if (processorURI.empty())
    {
        error(
            "RDE Cache: Device UUID={UUID} not found in rde_device_metadata.json, cannot create cache",
            "UUID", deviceUUID);
        throw InvalidArgument();
    }

    // Validate payload format if JSON encoding
    if (encodingFormat ==
        sdbusplus::xyz::openbmc_project::RDE::server::Manager::EncodingFormatType::
            JSON)
    {
        if (!payload.empty())
        {
            try
            {
                [[maybe_unused]] auto _ = nlohmann::json::parse(payload);
            }
            catch (const nlohmann::json::parse_error& e)
            {
                error(
                    "RDE Cache: Invalid JSON payload for UUID={UUID}: {MSG}",
                    "UUID", deviceUUID, "MSG", e.what());
                throw InvalidArgument();
            }
        }
    }

    // Build OperationInfo (eid will be set during replay from Device)
    OperationInfo opInfo{
        0,  // operationID will be generated during replay
        static_cast<OperationType>(operationType),
        targetURI,
        deviceUUID,
        0,  // eid will be retrieved from Device during replay
        payload,
        static_cast<PayloadFormatType>(payloadFormat),
        static_cast<EncodingFormatType>(encodingFormat),
        sessionId,
        ""  // opTaskPath will be generated during replay
    };

    // Call RDECacheManager::cacheOperation()
    auto& cacheManager = RDECacheManager::getInstance();
    if (!cacheManager.cacheOperation(opInfo))
    {
        error("RDE Cache: Failed to cache operation for device UUID={UUID}",
              "UUID", deviceUUID);
        throw InternalFailure();
    }

    info(
        "RDE Cache: Successfully created cache entry via D-Bus for UUID={UUID}, type={TYPE}, URI={URI}",
        "UUID", deviceUUID, "TYPE", static_cast<int>(operationType), "URI",
        targetURI);

    return true;
}

} // namespace pldm::rde

