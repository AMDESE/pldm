#include "manager.hpp"

#include "common/types.hpp"

#include <future>
#include <iostream>

PHOSPHOR_LOG2_USING;

namespace pldm::rde
{
Manager::Manager(sdbusplus::bus::bus& bus, pldm::InstanceIdDb* instanceIdDb,
                 pldm::requester::Handler<pldm::requester::Request>* handler) :
    sdbusplus::server::object::object<
        sdbusplus::xyz::openbmc_project::RDE::server::Manager>(
        bus, std::string(RDEManagerObjectPath).c_str()),
    instanceIdDb_(instanceIdDb), handler_(handler), bus_(bus)
{}

void Manager::handleMctpEndpoints(const std::vector<MctpInfo>& mctpInfos)
{
    for (const auto& mctpInfo : mctpInfos)
    {
        const auto& eid = std::get<0>(mctpInfo);
        const auto& uuid = std::get<1>(mctpInfo);

        // Construct D-Bus path using EID (safe for object path)
        std::string path =
            std::string(DeviceObjectPath) + "/" + std::to_string(eid);
        std::string friendlyName = "Device_" + std::to_string(eid);
        pldm::pdr::TerminusID tid =
            0; // TODO: Improve TID resolution logic if needed

        info(
            "Registering device UUID:{UUID} EID:{EID} Path:{PATH}, Name:{NAME}",
            "UUID", uuid, "EID", static_cast<int>(eid), "PATH", path, "NAME",
            friendlyName);

        // Create Device instance
        auto devicePtr = std::make_shared<Device>(bus_, path, instanceIdDb_,
                                                  handler_, eid, tid, uuid);

        // Populate context with smart pointer to ensure persistence
        DeviceContext context;
        context.uuid = uuid;
        context.eid = eid;
        context.deviceId = std::to_string(eid); // External identifier
        context.tid = tid;
        context.friendlyName = friendlyName;
        context.devicePtr = devicePtr;

        // Store in eid map to retain lifetime
        eidMap_[eid] = std::move(context);

        // Continue loop to register all endpoints
    }
}

DeviceContext* Manager::getDeviceContext(uint8_t eid)
{
    auto it = eidMap_.find(eid);
    if (it != eidMap_.end())
    {
        return &(it->second);
    }
    return nullptr;
}

std::tuple<std::string, uint16_t> Manager::performRedfishOperation(
    uint32_t /*requestId*/,
    sdbusplus::common::xyz::openbmc_project::rde::Common::OperationType
    /*operationId*/,
    std::string /*targetURI*/, std::string /*deviceId*/,
    std::string /*payload*/, PayloadFormatType /*payloadFormat*/,
    std::string /*encodingType*/, std::string /*sessionId*/)
{
    // TODO: Implement Redfish operation logic
    return {"{}", 200};
}

std::vector<std::map<std::string, std::string>> Manager::getDeviceSchemaInfo(
    std::string /*deviceId*/)
{
    // TODO: Implement schema info retrieval
    return {};
}

std::vector<sdbusplus::common::xyz::openbmc_project::rde::Common::OperationType>
    Manager::getSupportedOperations(std::string /*deviceId*/)
{
    // TODO: Implement supported operations retrieval
    return {};
}

} // namespace pldm::rde
