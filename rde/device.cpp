#include "device.hpp"

#include <filesystem>
#include <iostream>

namespace pldm::rde
{
Device::Device(sdbusplus::bus::bus& bus, const std::string& path,
               pldm::InstanceIdDb* instanceIdDb,
               pldm::requester::Handler<pldm::requester::Request>* handler,
               const uint8_t eid, uint8_t tid, const std::string& uuid,
               const std::vector<std::vector<uint8_t>>& pdrPayloads) :
    sdbusplus::server::object::object<
        sdbusplus::xyz::openbmc_project::RDE::server::Device>(
        bus, path.c_str()),
    instanceIdDb_(instanceIdDb), handler_(handler), eid_(eid), tid_(tid),
    uuid_(uuid), pdrPayloads_(pdrPayloads), currentState_(DeviceState::NotReady)
{
    info(
        "RDE : device Object creating device UUID:{UUID} EID:{EID} Path:{PATH}",
        "UUID", uuid, "EID", static_cast<int>(eid), "PATH", path);

    this->deviceUUID(uuid);
    this->name("Device_" + std::to_string(eid));
    this->negotiationStatus(NegotiationStatus::NotStarted);
}

Device::~Device()
{
    info("RDE: D-Bus Device object destroyed");
}

void Device::refreshDeviceInfo()
{
    info("RDE : Refreshing device EID:{EID}", "EID", static_cast<int>(eid_));

    resourceRegistry_ = std::make_unique<ResourceRegistry>();
    resourceRegistry_->loadFromResourcePDR(pdrPayloads_);

    session_ = std::make_unique<DiscoverySession>(*this);
    dictionaryManager_ = std::make_unique<pldm::rde::DictionaryManager>(uuid_);

    // Optional: Load annotation dictionary if needed
    try
    {
        std::string annotationPath = "/var/lib/pldm/annotations/annotation.bin";
        // TODO : "/var/lib/pldm/" + this->uuid() +
        // "/annotations/annotation.bin";
        if (std::filesystem::exists(annotationPath))
        {
            info(
                "RDE: Found annotation dictionary file at path:{PATH}, building now",
                "PATH", annotationPath);
            dictionaryManager_->buildAnnotationDictionary(annotationPath);
        }
        else
        {
            info(
                "RDE: Annotation dictionary file missing at path:{PATH}, skipping",
                "PATH", annotationPath);
        }
    }
    catch (const std::exception& e)
    {
        error("Failed to load annotation dictionary: Msg{MSG}", "MSG",
              e.what());
    }

    info("Discovery is in progress");
    session_->doNegotiateRedfish();

    // Placeholder for actual refresh logic
    std::map<std::string,
             std::variant<std::string, uint16_t, uint32_t, uint8_t>>
        changed;
    changed["Name"] = this->name();
    // emitDeviceUpdatedSignal(changed);
}

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

} // namespace pldm::rde
