#pragma once

#include "device_common.hpp"
#include "dictionary_manager.hpp"
#include "discov_session.hpp"
#include "operation_session.hpp"
#include "resource_registry.hpp"
#include "xyz/openbmc_project/Common/UUID/server.hpp"
#include "xyz/openbmc_project/RDE/Common/common.hpp"
#include "xyz/openbmc_project/RDE/Device/server.hpp"
#include "xyz/openbmc_project/RDE/Manager/server.hpp"
#ifdef OEM_AMD
#include "operation_task.hpp"
#include "rde_cache_manager.hpp"
#include "cache_manager_dbus.hpp"
#endif

#include <libpldm/base.h>
#include <libpldm/rde.h>

#include <common/instance_id.hpp>
#include <common/types.hpp>
#include <requester/handler.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server/object.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/source/event.hpp>

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <variant>
#include <vector>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>

namespace pldm::rde
{
class Manager; // Forward declaration
using VariantValue = std::variant<int64_t, std::string>;
using PropertyMap = std::map<std::string, VariantValue>;
using SchemaResourcesType = std::map<std::string, PropertyMap>;

using EntryIfaces = sdbusplus::server::object_t<
    sdbusplus::xyz::openbmc_project::RDE::server::Device,
    sdbusplus::xyz::openbmc_project::Common::server::UUID>;

#ifdef OEM_AMD
inline constexpr const char* rdePldmService = "xyz.openbmc_project.PLDM";
inline constexpr const char* rdeOperationTaskInterface =
    "xyz.openbmc_project.RDE.OperationTask";
inline constexpr const char* rdeTaskUpdatedMember = "TaskUpdated";

/** @brief D-Bus match rule for OperationTask TaskUpdated on @p objPath. */
inline std::string rdeOpTaskMatch(const std::string& objPath)
{
    return "type='signal',sender='" + std::string(rdePldmService) +
           "',interface='" + std::string(rdeOperationTaskInterface) +
           "',member='" + std::string(rdeTaskUpdatedMember) + "',path='" + objPath +
           "'";
}
#endif

/**
 * @class Device
 * @brief Represents a Redfish-capable device managed via D-Bus.
 */
class Device : public EntryIfaces, public std::enable_shared_from_this<Device>
{
  public:
    /**
     * @brief Constructor
     * @param[in] bus - The D-Bus bus object
     * @param[in] event         pldmd sd_event loop
     * @param[in] path - The D-Bus object path
     * @param[in] instanceIdDb  Pointer to the instance ID database used for
     *                          PLDM message tracking.
     * @param[in] handler       Pointer to the PLDM request handler for sending
     *                          and receiving messages.

     * @param[in] eid -  MCTP Endpoint ID used in PLDM stack
     * @param[in] tid - Target ID used in PLDM stack.
     * @param[in] uuid - Internal registration identifier.
     * @param[in] pdrPayloads Vector of raw Redfish Resource PDR payloads,
     *           each PDR as a vector of bytes (std::vector<uint8_t>)
     */
    Device(sdbusplus::bus::bus& bus, sdeventplus::Event& event,
           const std::string& path, pldm::InstanceIdDb* instanceIdDb,
           pldm::requester::Handler<pldm::requester::Request>* handler,
           const pldm::eid devEid, const pldm_tid_t tid, const pldm::UUID& uuid,
           const std::vector<std::vector<uint8_t>>& pdrPayloads);

    // Defaulted special member functions
    Device() = delete;
    Device(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(const Device&) = delete;
    Device& operator=(Device&&) = delete;
    virtual ~Device();

    /**
     * @brief Refreshes and updates capability and schema metadata from the
     * device.
     */
    void refreshDeviceInfo() override;

    void performRDEOperation(const OperationInfo& opInfo);

#ifdef OEM_AMD
    /**
     * @brief Check if the operation should be deferred (cached) instead of
     * executed.
     * @param opInfo Operation information
     * @return true if the operation was deferred, false otherwise
     */
    bool shouldDeferOperation(const OperationInfo& opInfo);

    /**
     * @brief Check if the operation is being replayed
     * @param opInfo Operation information
     * @return true if this is a replayed operation
     */
    bool isReplayOperation(const OperationInfo& opInfo) const;

    /**
     * @brief Check if operation can be cached (validates type, UUID, etc.)
     * @param opInfo Operation information
     * @return true if operation can be cached
     */
    bool canCacheOperation(const OperationInfo& opInfo) const;

    /**
     * @brief Cache the operation
     * @param opInfo Operation information
     * @param context Context string for logging (e.g., "during replay" or
     * "failed")
     * @return true if successfully cached
     */
    bool cacheOperation(const OperationInfo& opInfo,
                        const std::string& context);

    /**
     * @brief Start replaying cached operations for this device
     *
     * This method initializes the cache replay queue and starts replaying
     * cached operations asynchronously. Each operation is replayed one at a
     * time, and the next operation is processed after the current one completes
     * (success or failure).
     *
     * Should be called after negotiation is successful.
     * Cache entries are automatically cleared after being replayed.
     */
    void replayCachedOperations();

    /**
     * @brief Process the next cached operation in the queue
     *
     * This method is called when the current operation completes (success or
     * failure). It processes the next cache entry in the queue and clears the
     * completed one.
     */
    void processNextCachedOperation();

    /**
     * @brief Set the Manager reference for operation ID generation
     *
     * This method sets the Manager reference so that Device can use
     * the shared operation ID generator with conflict detection.
     *
     * @param[in] manager Pointer to the Manager instance
     */
    void setManager(Manager* manager);

    void setCacheManager(CacheManagerObject* manager);
    /**
     * @brief Stop cache replay and reset all replay state
     *
     * Clears the replay queue, resets current operation ID, and clears
     * the replay in progress flag. Used when replay must be stopped due to
     * errors.
     */
    void stopReplay();

    /**
     * @brief Send BIOS zero length command (RDEReplayComplete operationInit)
     *
     * This method sends the BIOS zero length command when cache replay
     * is complete or when there are no cached operations to replay.
     * Only sends if the device UUID is found in rde_device_metadata.json.
     */
    void sendBiosZeroLengthCommand();

    void sendBiosGetCommand();

    /**
     * @brief Complete Token READ from APCBDataTable via TaskUpdated (no host PLDM).
     * @return true if cached APCB data was emitted for this operation.
     */
    bool getAPCBTokenCache(const OperationInfo& opInfo);

#endif

    /**
     * @brief Access the device metadata.
     * @return Reference to metadata.
     */
    Metadata& getMetadata();

    /**
     * @brief Set the device metadata.
     * @param[in] meta Metadata to assign.
     */
    void setMetadata(const Metadata& meta);

    /**
     * @brief Get metadata field by key.
     *
     * This function retrieves an individual metadata field using its string
     * key. Supported types include: std::string, uint8_t, uint16_t, uint32_t,
     * FeatureSupport, and DeviceCapabilities.
     *
     * @param[in] key Metadata field name.
     * @return Variant holding the value of the metadata field.
     */
    MetadataVariant getMetadataField(const std::string& key) const;

    /**
     * @brief Set metadata field by key.
     *
     * This function assigns a value to a metadata field using its string key.
     * Supported types include: std::string, uint8_t, uint16_t, uint32_t,
     * FeatureSupport, and DeviceCapabilities.
     *
     * @param[in] key Metadata field name.
     * @param[in] value Variant containing the new value to set.
     */
    void setMetadataField(const std::string& key, const MetadataVariant& value);

    /**
     * @brief Returns reference to the PLDM instance ID database.
     * @return Reference to pldm::InstanceIdDb
     */
    inline pldm::InstanceIdDb& getInstanceIdDb()
    {
        return *instanceIdDb_;
    }

    /**
     * @brief Returns a pointer to the PLDM request handler.
     * @return Pointer to Handler of PLDM Request
     */
    inline pldm::requester::Handler<pldm::requester::Request>* getHandler()
    {
        return handler_;
    }

    /**
     * @brief Retrieves the Terminus ID (TID) associated with this instance.
     *
     * This method returns the value of the private member variable that stores
     * the PLDM Terminus ID, used to identify the device during communication.
     *
     * @return pldm_tid_t The stored Terminus ID value.
     */
    inline pldm_tid_t getTid() const
    {
        return tid_;
    }

    /**
     * @brief Attempts to update the device state.
     * @param newState The desired DeviceState.
     */
    void updateState(DeviceState newState);

    /**
     * @brief Returns the current device state.
     */
    DeviceState getState() const;

    /**
     * @brief Retrieve a non-owning pointer to the ResourceRegistry.
     *
     * @return Raw pointer to ResourceRegistry, or nullptr if not initialized.
     */
    ResourceRegistry* getRegistry()
    {
        return resourceRegistry_.get();
    }

    /**
     * @brief Retrieve a non-owning pointer to the DictionaryManager.
     *
     * @return Raw pointer to DictionaryManager, or nullptr if not initialized.
     */
    DictionaryManager* getDictionaryManager()
    {
        return dictionaryManager_.get();
    }

    /**
     * @brief Gets the reference to the sdeventplus::Event object.
     *
     * This function returns a reference to the internal sdeventplus::Event
     * instance used by this class. Use this to interact with the underlying
     * event loop.
     *
     * @return Reference to the sdeventplus::Event object.
     */
    sdeventplus::Event& getEvent()
    {
        return event_;
    }

    /**
     * @brief Get the D-Bus connection reference.
     *
     * @return Reference to the D-Bus connection.
     */
    inline sdbusplus::bus::bus& getBus()
    {
        return bus_;
    }

    void shutdown();

  private:
#ifdef OEM_AMD
    /**
     * @brief URI for SocConfiguration/Token
     */
    static constexpr const char* SocConfigurationTokenURI =
        "Oem/AMD/SocConfiguration/Token";
#endif
    /**
     * @brief Constructs schema resource payload based on discovered resources.
     *
     * Iterates through the registry and transforms each ResourceInfo into a
     * property map keyed by canonical field names (defined by
     * SchemaResourceKeys).
     *
     * Includes metadata such as subUri, schemaName, schemaVersion, schemaClass,
     * operations count, container name, and placeholders for extensions or
     * actions.
     *
     * @return A schema resource map suitable for D-Bus publication.
     */
    SchemaResourcesType buildSchemaResourcesPayload() const;

    Metadata metaData_;
    pldm::InstanceIdDb* instanceIdDb_ = nullptr;
    pldm::requester::Handler<pldm::requester::Request>* handler_ = nullptr;
    sdbusplus::bus::bus& bus_;
    sdeventplus::Event& event_;
    pldm_tid_t tid_;
    /** @brief  Redfish Resource PDR list blob **/
    std::vector<std::vector<uint8_t>> pdrPayloads_;
    DeviceState currentState_;
    std::unique_ptr<ResourceRegistry> resourceRegistry_;
    std::unique_ptr<pldm::rde::DictionaryManager> dictionaryManager_;
    std::unique_ptr<DiscoverySession> discovSession_;
    std::unique_ptr<OperationSession> opSession_;
#ifdef OEM_AMD
    std::unique_ptr<sdbusplus::bus::match_t> biosGetTaskUpdatedMatch_;

    void handleBiosGetTaskSignal(sdbusplus::message::message& msg,
                                 uint32_t operationID,
                                 std::shared_ptr<std::string> payloadBuffer);
    // Current operation ID being replayed
    uint32_t currentReplayOperationId_ = 0;
    // Current operation timestamp being replayed (used as key for completion)
    uint64_t currentReplayTimestamp_ = 0;
    // Flag to track if cache replay is in progress
    bool isReplayInProgress_ = false;
    // Manager reference for shared operation ID generation
    Manager* manager_ = nullptr;

    CacheManagerObject* cacheManager_ = nullptr;
    // Signal match for TaskUpdated to track operation completion
    std::unique_ptr<sdbusplus::bus::match_t> taskUpdatedMatch_;
    // Defer APCB TaskUpdated until after StartRedfishOperation returns to bmcweb
    std::unique_ptr<sdeventplus::source::Defer> deferredApcbTaskSignal_;
#endif
    bool shuttingDown_;
};

} // namespace pldm::rde
