#pragma once

#include "rde/device_common.hpp"

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pldm::rde
{

/**
 * @enum CacheStatus
 * @brief Represents the current processing state of a cache entry
 */
enum class CacheStatus
{
    Pending,   // Waiting for replay
    Processing, // Currently being replayed
    Failed     // Replay failed
};

/**
 * @struct CacheEntry
 * @brief Represents a cached RDE operation entry
 */
struct CacheEntry
{
    std::string deviceUUID;
    OperationType operationType;
    std::string targetURI;
    std::string payload;
    PayloadFormatType payloadFormat;
    EncodingFormatType encodingType;
    std::string sessionId;
    uint64_t timestamp;  // Cache creation timestamp
    CacheStatus status = CacheStatus::Pending;

    CacheEntry() = default;
    CacheEntry(const std::string& uuid, OperationType opType,
               const std::string& uri, const std::string& pay,
               PayloadFormatType pFormat, EncodingFormatType eFormat,
               const std::string& sessId) :
        deviceUUID(uuid),
        operationType(opType), targetURI(uri), payload(pay),
        payloadFormat(pFormat), encodingType(eFormat), sessionId(sessId),
        timestamp(std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count()),
        status(CacheStatus::Pending)
    {}
};

/**
 * @class RDECacheManager
 * @brief Manages cache for failed RDE operations using UUID as the base key
 */
class RDECacheManager
{
  public:
    RDECacheManager() = default;
    ~RDECacheManager() = default;
    RDECacheManager(const RDECacheManager&) = delete;
    RDECacheManager& operator=(const RDECacheManager&) = delete;
    RDECacheManager(RDECacheManager&&) = delete;
    RDECacheManager& operator=(RDECacheManager&&) = delete;

    /**
     * @brief Get the singleton instance of RDECacheManager
     * @return Reference to the singleton instance
     */
    static RDECacheManager& getInstance();

    /**
     * @brief Cache a failed RDE operation using OperationInfo
     */
    bool cacheOperation(const OperationInfo& opInfo);

    /**
     * @brief Get the next pending or failed operation for replay and mark it as processing
     * @param[in] deviceUUID Device UUID to query
     * @return The next pending or failed cache entry, or std::nullopt if none exist
     */
    std::optional<CacheEntry>
        markNextPendingForProcessing(const std::string& deviceUUID);

    /**
     * @brief Mark an operation as completed and remove it from cache
     * @param[in] deviceUUID Device UUID
     * @param[in] timestamp Unique timestamp of the entry to remove
     */
    void completeOperation(const std::string& deviceUUID, uint64_t timestamp);

    /**
     * @brief Mark an operation as failed and keep it in cache
     * @param[in] deviceUUID Device UUID
     * @param[in] timestamp Unique timestamp of the entry to mark as failed
     */
    void markOperationAsFailed(const std::string& deviceUUID, uint64_t timestamp);

    /**
     * @brief Reset all 'Processing' entries back to 'Pending'
     * @param[in] deviceUUID Device UUID
     */
    void resetProcessingToPending(const std::string& deviceUUID);

    /**
     * @brief Get a copy of all cached operations (including those in processing)
     * @param[in] deviceUUID Device UUID to query
     * @return Vector of cache entries
     */
    std::vector<CacheEntry> getCache(const std::string& deviceUUID) const;

    /**
     * @brief Convert CacheEntry to OperationInfo for replay
     */
    static OperationInfo toOperationInfo(const CacheEntry& entry,
                                         uint32_t operationID, pldm::eid eid);

  private:
    // Map: deviceUUID -> vector of CacheEntry
    std::map<std::string, std::vector<CacheEntry>> cache_;
};

} // namespace pldm::rde
