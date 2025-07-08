#include "manager.hpp"

#include "common/types.hpp"

#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>
#include <xyz/openbmc_project/PLDM/Event/server.hpp>

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
        const uint8_t eid = std::get<0>(mctpInfo);
        const std::string& uuid = std::get<1>(mctpInfo);

        info("Registering device UUID:{UUID} EID:{EID} ", "UUID", uuid, "EID",
             static_cast<int>(eid));

        // Skip if already registered
        if (signalMatches_.count(eid))
            continue;

        auto match = std::make_unique<sdbusplus::bus::match_t>(
            bus_,
            sdbusplus::bus::match::rules::type::signal() +
                sdbusplus::bus::match::rules::member("DiscoveryComplete") +
                sdbusplus::bus::match::rules::interface(
                    "xyz.openbmc_project.PLDM.Event") +
                sdbusplus::bus::match::rules::path("/xyz/openbmc_project/pldm"),
            [this, eid, uuid](sdbusplus::message::message& msg) {
                uint8_t signalTid = 0;

                msg.read(signalTid);

                // Avoid duplicate registration
                if (!eidMap_.count(eid))
                {
                    this->createDeviceDbusObject(eid, uuid, signalTid);
                }

                // Optional: remove match after use
                signalMatches_.erase(eid);
            });

        signalMatches_[eid] = std::move(match);
    }
}

void Manager::createDeviceDbusObject(uint8_t eid, const std::string& uuid,
                                     pldm_tid_t tid)
{
    // Prevent duplicate creation for the same EID
    if (eidMap_.count(eid))
    {
        info("Device for EID already exists. Skipping registration.");
        return;
    }

    std::string path =
        std::string(DeviceObjectPath) + "/" + std::to_string(eid);
    std::string friendlyName = "Device_" + std::to_string(eid);

    // Create base device
    auto devicePtr = std::make_shared<Device>(bus_, path, instanceIdDb_,
                                              handler_, eid, tid, uuid);

    DeviceContext context;
    context.uuid = uuid;
    context.eid = eid;
    context.tid = tid;
    context.deviceId = std::to_string(eid);
    context.friendlyName = friendlyName;
    context.devicePtr = devicePtr;

    info("RDE device created UUID:{UUID} EID:{EID} Path:{PATH}, Name:{NAME}",
         "UUID", uuid, "EID", static_cast<int>(eid), "PATH", path, "NAME",
         friendlyName);

    eidMap_[eid] = std::move(context);

    devicePtr->refreshDeviceInfo();
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
