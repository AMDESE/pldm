#include "rde_cache_manager.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <algorithm>
#include <chrono>

PHOSPHOR_LOG2_USING;

namespace pldm::rde
{

RDECacheManager& RDECacheManager::getInstance()
{
    static RDECacheManager instance;
    return instance;
}

bool RDECacheManager::cacheOperation(const OperationInfo& opInfo)
{
    auto& deviceCache = cache_[opInfo.deviceUUID];

    for (auto& entry : deviceCache)
    {
        if (entry.status == CacheStatus::Pending &&
            entry.operationType == opInfo.operationType &&
            entry.targetURI == opInfo.targetURI)
        {
            try
            {
                auto jExisting = nlohmann::json::parse(entry.payload);
                auto jNew = nlohmann::json::parse(opInfo.payload);

                jExisting.update(jNew);

                entry.payload = jExisting.dump();
                entry.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();

                info("RDE Cache: Merged payload for UUID={UUID}, URI={URI}",
                     "UUID", opInfo.deviceUUID, "URI", entry.targetURI);
                return true;
            }
            catch (...)
            {
                break;
            }
        }
    }

    try
    {
        CacheEntry entry(opInfo.deviceUUID, opInfo.operationType,
                         opInfo.targetURI, opInfo.payload,
                         opInfo.payloadFormat, opInfo.encodingType,
                         opInfo.sessionId);
        deviceCache.push_back(std::move(entry));

        info(
            "RDE Cache: Added new cache entry for UUID={UUID}, type={TYPE}, URI={URI}, payload_size={SIZE}, total_entries={COUNT}",
            "UUID", opInfo.deviceUUID, "TYPE",
            static_cast<int>(opInfo.operationType), "URI", opInfo.targetURI,
            "SIZE", opInfo.payload.size(), "COUNT", deviceCache.size());

        return true;
    }
    catch (const std::exception& e)
    {
        error("RDE Cache: Failed to cache operation for UUID={UUID}: {MSG}",
              "UUID", opInfo.deviceUUID, "MSG", e.what());
        return false;
    }
}

std::optional<CacheEntry>
    RDECacheManager::markNextPendingForProcessing(const std::string& deviceUUID)
{
    auto it = cache_.find(deviceUUID);
    if (it == cache_.end())
    {
        return std::nullopt;
    }

    for (auto& entry : it->second)
    {
        entry.status = CacheStatus::Processing;
        return entry; // Return a copy
    }

    return std::nullopt;
}

void RDECacheManager::completeOperation(const std::string& deviceUUID,
                                         uint64_t timestamp)
{
    auto it = cache_.find(deviceUUID);
    if (it == cache_.end())
    {
        return;
    }

    auto& deviceCache = it->second;
    auto entryIt = std::find_if(deviceCache.begin(), deviceCache.end(),
                                [timestamp](const CacheEntry& e) {
                                    return e.timestamp == timestamp;
                                });

    if (entryIt != deviceCache.end())
    {
        info("RDE Cache: Operation completed and removed from cache for UUID={UUID}, URI={URI}",
             "UUID", deviceUUID, "URI", entryIt->targetURI);
        deviceCache.erase(entryIt);
    }
}

void RDECacheManager::markOperationAsFailed(const std::string& deviceUUID,
                                            uint64_t timestamp)
{
    auto it = cache_.find(deviceUUID);
    if (it == cache_.end())
    {
        return;
    }

    auto& deviceCache = it->second;
    auto entryIt = std::find_if(deviceCache.begin(), deviceCache.end(),
                                [timestamp](const CacheEntry& e) {
                                    return e.timestamp == timestamp;
                                });

    if (entryIt != deviceCache.end())
    {
        entryIt->status = CacheStatus::Failed;
        info("RDE Cache: Operation marked as failed for UUID={UUID}, URI={URI}",
             "UUID", deviceUUID, "URI", entryIt->targetURI);
    }
}

void RDECacheManager::resetProcessingToPending(const std::string& deviceUUID)
{
    auto it = cache_.find(deviceUUID);
    if (it == cache_.end())
    {
        return;
    }

    for (auto& entry : it->second)
    {
        if (entry.status == CacheStatus::Processing)
        {
            entry.status = CacheStatus::Pending;
        }
    }
    info("RDE Cache: Reset all 'Processing' entries to 'Pending' for UUID={UUID}",
         "UUID", deviceUUID);
}

std::vector<CacheEntry>
    RDECacheManager::getCache(const std::string& deviceUUID) const
{
    auto it = cache_.find(deviceUUID);
    if (it != cache_.end())
    {
        return it->second;
    }
    return {};
}

OperationInfo RDECacheManager::toOperationInfo(const CacheEntry& entry,
                                                uint32_t operationID,
                                                pldm::eid eid)
{
    std::string taskPathStr =
        "/xyz/openbmc_project/RDE/OperationTask/" + std::to_string(operationID);

    OperationInfo opInfo{
        operationID,        entry.operationType, entry.targetURI,
        entry.deviceUUID,   eid,                 entry.payload,
        entry.payloadFormat, entry.encodingType, entry.sessionId,
        taskPathStr};

    return opInfo;
}

} // namespace pldm::rde
