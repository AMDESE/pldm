#include "device.hpp"

#ifdef OEM_AMD
#include "manager.hpp"
#include "rde_cache_manager.hpp"
#include "utils.hpp"

#include <sdbusplus/bus/match.hpp>
#endif

#include <filesystem>

namespace pldm::rde
{
Device::Device(sdbusplus::bus::bus& bus, sdeventplus::Event& event,
               const std::string& path, pldm::InstanceIdDb* instanceIdDb,
               pldm::requester::Handler<pldm::requester::Request>* handler,
               const pldm::eid devEid, const pldm_tid_t tid,
               const pldm::UUID& uuid,
               const std::vector<std::vector<uint8_t>>& pdrPayloads) :
    EntryIfaces(bus, path.c_str()), instanceIdDb_(instanceIdDb),
    handler_(handler), bus_(bus), event_(event), tid_(tid),
    pdrPayloads_(pdrPayloads), currentState_(DeviceState::NotReady)
{
    info(
        "RDE : device Object creating device UUID:{UUID} EID:{EID} Path:{PATH}",
        "UUID", uuid, "EID", static_cast<int>(devEid), "PATH", path);

    this->eid(devEid);
    this->deviceUUID(uuid);
    this->name("Device_" + std::to_string(devEid));
    this->negotiationStatus(NegotiationStatus::NotStarted);
}

Device::~Device()
{
    info("RDE : D-Bus Device object destroyed UUID:{UUID} EID:{EID}", "UUID",
         deviceUUID(), "EID", static_cast<int>(eid()));
}

void Device::refreshDeviceInfo()
{
    info("RDE : Refreshing device EID:{EID}", "EID", static_cast<int>(eid()));

    try
    {
        resourceRegistry_ = std::make_unique<ResourceRegistry>(eid(), this);
        resourceRegistry_->loadFromResourcePDR(pdrPayloads_);
        schemaResources(buildSchemaResourcesPayload());

        dictionaryManager_ =
            std::make_unique<pldm::rde::DictionaryManager>(deviceUUID());

        std::shared_ptr<Device> self;
        try
        {
            self = shared_from_this();
        }
        catch (const std::bad_weak_ptr& e)
        {
            error("Device shared_from_this() failed: Msg={MSG}", "MSG",
                  e.what());
            return;
        }

        debug(
            "RDE: Discovery sequence started for EID={EID}, use_count={COUNT}",
            "EID", static_cast<int>(eid()), "COUNT", self.use_count());

        discovSession_ = std::make_unique<DiscoverySession>(self);
        this->negotiationStatus(NegotiationStatus::InProgress);
        discovSession_->doNegotiateRedfish();
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        error("refreshDeviceInfo D-Bus error: Msg={MSG}", "MSG", e.what());
        this->negotiationStatus(NegotiationStatus::Failed);
        return;
    }
    catch (const std::exception& e)
    {
        error("refreshDeviceInfo: Failed : Msg={MSG}", "MSG", e.what());
        this->negotiationStatus(NegotiationStatus::Failed);
        return;
    }

    std::map<std::string,
             std::variant<std::string, uint16_t, uint32_t, uint8_t>>
        changed;
    changed["Name"] = name(); // Simplified access

    deviceUpdated();
}

void Device::performRDEOperation(const OperationInfo& oipInfo)
{
    info("Operation Session Started");

#ifdef OEM_AMD
    if (shouldDeferOperation(oipInfo))
    {
        return;
    }
#endif

    std::shared_ptr<Device> self;
    try
    {
        self = shared_from_this();
        if (!self)
        {
            error("Device::shared_from_this() returned null shared_ptr");
            return;
        }
    }
    catch (const std::bad_weak_ptr& e)
    {
        error("Device shared_from_this() failed: Msg={MSG}", "MSG", e.what());
        return;
    }

    try
    {
        opSession_ = std::make_unique<OperationSession>(self, oipInfo);
        if (!opSession_)
        {
            error("OperationSession creation failed");
            return;
        }

        info("Operation is in progress");
        opSession_->doOperationInit();
    }
    catch (const std::exception& e)
    {
        error("OperationSession setup failed: Msg={MSG}", "MSG", e.what());
#ifdef OEM_AMD
        // Cache failed operations (skip replayed operations to avoid infinite
        // loop)
        if (!isReplayOperation(oipInfo) && canCacheOperation(oipInfo))
        {
            cacheOperation(oipInfo, "failed");
        }
#endif
        return;
    }
}

#ifdef OEM_AMD
bool Device::shouldDeferOperation(const OperationInfo& oipInfo)
{
    if (isReplayOperation(oipInfo))
    {
        return false;
    }

    if (isReplayInProgress_)
    {
        if (canCacheOperation(oipInfo))
        {
            cacheOperation(oipInfo, "during replay");
            return true; // Indicate that it has been deferred
        }
    }
    return false;
}

bool Device::isReplayOperation(const OperationInfo& opInfo) const
{
    return (currentReplayOperationId_ != 0 &&
            opInfo.operationID == currentReplayOperationId_);
}

bool Device::canCacheOperation(const OperationInfo& opInfo) const
{
    std::string processorURI = loadProcessorURI(opInfo.deviceUUID);
    if (processorURI.empty())
    {
        info(
            "RDE Cache: Device UUID={UUID} not found in rde_device_metadata.json, skipping cache",
            "UUID", opInfo.deviceUUID);
        return false;
    }

    // Don't cache BIOS zero length command
    if (opInfo.targetURI == SocConfigurationTokenURI &&
        opInfo.payload.empty() && opInfo.operationType == OperationType::UPDATE)
    {
        return false;
    }

    return true;
}

bool Device::cacheOperation(const OperationInfo& opInfo,
                            const std::string& context)
{
    auto& cacheManager = RDECacheManager::getInstance();
    if (!cacheManager.cacheOperation(opInfo))
    {
        error("RDE Cache: Failed to cache operation for device UUID={UUID}",
              "UUID", opInfo.deviceUUID);
        return false;
    }

    if (isReplayInProgress_)
    {
        info(
            "RDE Cache: Cached operation {CONTEXT} for device UUID={UUID}, EID={EID} (will be processed after replay completes)",
            "CONTEXT", context, "UUID", opInfo.deviceUUID, "EID", opInfo.eid);
    }
    else
    {
        info(
            "RDE Cache: Successfully cached {CONTEXT} operation for device UUID={UUID}, EID={EID}",
            "CONTEXT", context, "UUID", opInfo.deviceUUID, "EID", opInfo.eid);
    }
    return true;
}

void Device::replayCachedOperations()
{
    if (isReplayInProgress_)
    {
        return;
    }

    isReplayInProgress_ = true;

    // Setup signal match to listen for TaskUpdated signals
    if (!taskUpdatedMatch_)
    {
        std::weak_ptr<Device> weakSelf = shared_from_this();
        taskUpdatedMatch_ = std::make_unique<sdbusplus::bus::match_t>(
            bus_,
            sdbusplus::bus::match::rules::type::signal() +
                sdbusplus::bus::match::rules::member("TaskUpdated") +
                sdbusplus::bus::match::rules::interface(
                    "xyz.openbmc_project.RDE.OperationTask"),
            [weakSelf](sdbusplus::message::message& msg) {
                auto self = weakSelf.lock();
                if (!self)
                {
                    return;
                }

                std::string path = msg.get_path();

                if (self->currentReplayOperationId_ == 0)
                {
                    return;
                }

                std::string expectedPath =
                    "/xyz/openbmc_project/RDE/OperationTask/" +
                    std::to_string(self->currentReplayOperationId_);
                if (path != expectedPath)
                {
                    return;
                }

                // Extract return code from signal
                std::map<std::string, std::variant<std::string, uint16_t>>
                    changed;
                msg.read(changed);

                auto it = changed.find("return_code");
                if (it == changed.end())
                {
                    return;
                }

                uint16_t returnCode = std::get<uint16_t>(it->second);

                if (returnCode ==
                    static_cast<uint16_t>(OpState::OperationCompleted))
                {
                    // Success: delete cache entry
                    if (self->currentReplayTimestamp_ != 0)
                    {
                        RDECacheManager::getInstance().completeOperation(
                            self->deviceUUID(), self->currentReplayTimestamp_);
                        self->currentReplayTimestamp_ = 0;
                    }
                    info(
                        "RDE Cache Replay: Operation {OID} succeeded (TaskStatus=OperationCompleted), cache entry removed, processing next operation",
                        "OID", self->currentReplayOperationId_);

                    self->currentReplayOperationId_ = 0;
                    self->processNextCachedOperation();
                }
                else if (returnCode ==
                             static_cast<uint16_t>(OpState::OperationFailed) ||
                         returnCode ==
                             static_cast<uint16_t>(OpState::Cancelled) ||
                         returnCode == static_cast<uint16_t>(OpState::TimedOut))
                {
                    // Failure: mark as failed and continue to next
                    if (self->currentReplayTimestamp_ != 0)
                    {
                        RDECacheManager::getInstance().markOperationAsFailed(
                            self->deviceUUID(), self->currentReplayTimestamp_);
                        self->currentReplayTimestamp_ = 0;
                    }
                    info(
                        "RDE Cache Replay: Operation {OID} failed with TaskStatus={CODE}, marked as failed, processing next operation",
                        "OID", self->currentReplayOperationId_, "CODE",
                        returnCode);

                    self->currentReplayOperationId_ = 0;
                    self->processNextCachedOperation();
                }
                else
                {
                    info(
                        "RDE Cache Replay: Operation {OID} status updated to {CODE}, waiting for final state",
                        "OID", self->currentReplayOperationId_, "CODE",
                        returnCode);
                }
            });
    }

    // Start processing the first cache entry
    processNextCachedOperation();
}

void Device::processNextCachedOperation()
{
    auto& cacheManager = RDECacheManager::getInstance();
    auto entryOpt = cacheManager.markNextPendingForProcessing(deviceUUID());

    if (!entryOpt.has_value())
    {
        // No more pending cached operations, replay is complete
        info(
            "RDE Cache Replay: All cached operations processed for UUID={UUID}, sending BIOS zero length command",
            "UUID", deviceUUID());
        currentReplayOperationId_ = 0;
        currentReplayTimestamp_ = 0;
        isReplayInProgress_ = false;
        sendBiosZeroLengthCommand();
        return;
    }

    CacheEntry entry = *entryOpt;
    currentReplayTimestamp_ = entry.timestamp;

    info(
        "RDE Cache Replay: Replaying operation type={TYPE}, URI={URI}, payload={PAYLOAD}, timestamp={TS}",
        "TYPE", static_cast<int>(entry.operationType), "URI", entry.targetURI,
        "PAYLOAD", entry.payload, "TS", entry.timestamp);

    try
    {
        if (!manager_)
        {
            error(
                "RDE Cache Replay: Manager not set, cannot generate operation ID, stopping replay for UUID={UUID}",
                "UUID", deviceUUID());
            stopReplay();
            return;
        }

        uint32_t operationID = manager_->getNextAvailableOperationId();
        if (operationID == 0)
        {
            error(
                "RDE Cache Replay: Failed to generate operation ID (all IDs in use), stopping replay for UUID={UUID}",
                "UUID", deviceUUID());
            stopReplay();
            return;
        }

        currentReplayOperationId_ = operationID;

        OperationInfo opInfo =
            RDECacheManager::toOperationInfo(entry, operationID, eid());

        auto task = std::make_shared<OperationTask>(bus_, opInfo.opTaskPath);
        manager_->registerOperationTask(operationID, task);
        performRDEOperation(opInfo);
    }
    catch (const std::exception& e)
    {
        error(
            "RDE Cache Replay: Failed to replay operation for UUID={UUID}: {MSG}, continuing with next",
            "UUID", deviceUUID(), "MSG", e.what());

        // Mark failed-to-start operation as complete so we can move to next
        cacheManager.completeOperation(deviceUUID(), currentReplayTimestamp_);
        currentReplayOperationId_ = 0;
        currentReplayTimestamp_ = 0;
        processNextCachedOperation();
    }
}

void Device::stopReplay()
{
    RDECacheManager::getInstance().resetProcessingToPending(deviceUUID());
    currentReplayOperationId_ = 0;
    currentReplayTimestamp_ = 0;
    isReplayInProgress_ = false;
}

void Device::sendBiosZeroLengthCommand()
{
    // Check if UUID exists in rde_device_metadata.json
    std::string processorURI = loadProcessorURI(deviceUUID());
    if (processorURI.empty())
    {
        info(
            "RDE Cache Replay: Device UUID={UUID} not found in rde_device_metadata.json, skipping BIOS zero length command",
            "UUID", deviceUUID());
        return;
    }

    // Send BIOS zero length command when cache replay is complete
    if (!manager_)
    {
        error(
            "RDE Cache Replay: Manager not set, cannot send BIOS zero length command for UUID={UUID}",
            "UUID", deviceUUID());
        return;
    }

    try
    {
        uint32_t operationID = manager_->getNextAvailableOperationId();
        if (operationID == 0)
        {
            error(
                "RDE Cache Replay: Failed to generate operation ID for BIOS zero length command, UUID={UUID}",
                "UUID", deviceUUID());
            return;
        }

        OperationType operationType = OperationType::UPDATE;
        std::string subURI = SocConfigurationTokenURI;
        std::string payload = "";
        PayloadFormatType payloadFormat = PayloadFormatType::Inline;
        EncodingFormatType encodingType = EncodingFormatType::JSON;
        std::string sessionID = manager_->getJSONSchema();
        if (sessionID.empty())
        {
            error(
                "RDE Cache Replay: Cannot find session ID for BIOS zero length command, UUID={UUID}",
                "UUID", deviceUUID());
            return;
        }

        std::string taskPathStr = "/xyz/openbmc_project/RDE/OperationTask/" +
                                  std::to_string(operationID);

        OperationInfo opInfo{operationID,   operationType, subURI,
                             deviceUUID(),  eid(),         payload,
                             payloadFormat, encodingType,  sessionID,
                             taskPathStr};

        auto task = std::make_shared<OperationTask>(bus_, opInfo.opTaskPath);
        manager_->registerOperationTask(operationID, task);

        std::shared_ptr<Device> self = shared_from_this();
        opSession_ = std::make_unique<OperationSession>(self, opInfo);
        if (!opSession_)
        {
            error(
                "RDE Cache Replay: Failed to create OperationSession for BIOS zero length command, UUID={UUID}",
                "UUID", deviceUUID());
            return;
        }

        info(
            "RDE Cache Replay: Sending BIOS zero length command for UUID={UUID}, OperationID={OID}",
            "UUID", deviceUUID(), "OID", operationID);
        opSession_->doOperationInit();
    }
    catch (const std::exception& e)
    {
        error(
            "RDE Cache Replay: Failed to send BIOS zero length command for UUID={UUID}: {MSG}",
            "UUID", deviceUUID(), "MSG", e.what());
    }
}

void Device::setManager(Manager* manager)
{
    manager_ = manager;
}
#endif

Metadata& Device::getMetadata()
{
    return metaData_;
}

void Device::setMetadata(const Metadata& meta)
{
    metaData_ = meta;
}

MetadataVariant Device::getMetadataField(const std::string& key) const
{
    if (key == "devProviderName")
        return metaData_.devProviderName;
    if (key == "etag")
        return metaData_.etag;
    if (key == "devConfigSignature")
        return metaData_.devConfigSignature;
    if (key == "mcMaxTransferChunkSizeBytes")
        return metaData_.mcMaxTransferChunkSizeBytes;
    if (key == "devMaxTransferChunkSizeBytes")
        return metaData_.devMaxTransferChunkSizeBytes;
    if (key == "mcConcurrencySupport")
        return metaData_.mcConcurrencySupport;
    if (key == "deviceConcurrencySupport")
        return metaData_.deviceConcurrencySupport;
    if (key == "protocolVersion")
        return metaData_.protocolVersion;
    if (key == "encoding")
        return metaData_.encoding;
    if (key == "sessionId")
        return metaData_.sessionId;
    if (key == "mcFeatureSupport")
        return metaData_.mcFeatureSupport;
    if (key == "devFeatureSupport")
        return metaData_.devFeatureSupport;
    if (key == "devCapabilities")
        return metaData_.devCapabilities;
    return std::string{};
}

void Device::setMetadataField(const std::string& key,
                              const MetadataVariant& value)
{
    try
    {
        if (key == "devProviderName")
            metaData_.devProviderName = std::get<std::string>(value);
        else if (key == "etag")
            metaData_.etag = std::get<std::string>(value);
        else if (key == "devConfigSignature")
            metaData_.devConfigSignature = std::get<uint32_t>(value);
        else if (key == "mcMaxTransferChunkSizeBytes")
            metaData_.mcMaxTransferChunkSizeBytes = std::get<uint32_t>(value);
        else if (key == "devMaxTransferChunkSizeBytes")
            metaData_.devMaxTransferChunkSizeBytes = std::get<uint32_t>(value);
        else if (key == "deviceConcurrencySupport")
            metaData_.deviceConcurrencySupport = std::get<uint8_t>(value);
        else if (key == "mcConcurrencySupport")
            metaData_.mcConcurrencySupport = std::get<uint8_t>(value);
        else if (key == "protocolVersion")
            metaData_.protocolVersion = std::get<std::string>(value);
        else if (key == "encoding")
            metaData_.encoding = std::get<std::string>(value);
        else if (key == "sessionId")
            metaData_.sessionId = std::get<std::string>(value);
        else if (key == "mcFeatureSupport")
            metaData_.mcFeatureSupport = std::get<FeatureSupport>(value);
        else if (key == "devFeatureSupport")
            metaData_.devFeatureSupport = std::get<FeatureSupport>(value);
        else if (key == "devCapabilities")
            metaData_.devCapabilities = std::get<DeviceCapabilities>(value);
        else
            error("Unknown metadata key:{KEY}", "KEY", key);
    }
    catch (const std::bad_variant_access& ex)
    {
        error("Metadata type mismatch for key {KEY}: {WHAT}", "KEY", key,
              "WHAT", ex.what());
    }
    catch (const std::exception& ex)
    {
        error("Failed to set metadata key {KEY}: {WHAT}", "KEY", key, "WHAT",
              ex.what());
    }
}

DeviceState Device::getState() const
{
    return currentState_;
}

void Device::updateState(DeviceState newState)
{
    currentState_ = newState;
}

SchemaResourcesType Device::buildSchemaResourcesPayload() const
{
    SchemaResourcesType payload;

    if (!resourceRegistry_)
        return payload;

    const auto& resourceMap = resourceRegistry_->getResourceMap();

    for (const auto& [resourceId, info] : resourceMap)
    {
        PropertyMap entry;

        // Canonical schema keys from SchemaResourceKeys definition
        entry["subUri"] = info.uri;
        entry["schemaName"] = info.schemaName;
        entry["schemaVersion"] = info.schemaVersion;
        entry["schemaClass"] = static_cast<int64_t>(info.schemaClass);
        entry["ProposedContainingResourceName"] = info.propContainResourceName;
        entry["operations"] =
            static_cast<int64_t>(info.operations.size()); // Count for now

        // Optional/OEM extensions: placeholders (can be populated when
        // available)
        entry["OEMExtensions"] =
            std::string{}; // Replace with actual logic if applicable
        entry["Actions"] =
            std::string{}; // Replace with action serialization when added
        entry["ActionName"] =
            std::string{}; // Optional if action metadata is tracked
        entry["ActionPath"] = std::string{}; // Optional action URI

        payload[resourceId] = std::move(entry);
    }

    return payload;
}

} // namespace pldm::rde
