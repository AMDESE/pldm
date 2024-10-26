#pragma once
#include "../../common/utils.hpp"
#include "../../libpldmresponder/base.hpp"
#include "../../libpldmresponder/bios.hpp"
#include "../../libpldmresponder/fru.hpp"
#include "../../libpldmresponder/platform.hpp"
#include "../../oem/ampere/event/oem_event_manager.hpp"
#include "../../platform-mc/manager.hpp"
#include "../../pldmd/invoker.hpp"
#include "../../requester/request.hpp"

namespace pldm
{
namespace oem_ampere
{

/**
 * @class OemAMPERE
 *
 * @brief class for creating all the OEM AMPERE handlers
 *
 *  Only in case of OEM_AMPERE this class object will be instantiated
 */
class OemAMPERE
{
  public:
    OemAMPERE() = delete;
    OemAMPERE& operator=(const OemAMPERE&) = delete;
    OemAMPERE(OemAMPERE&&) = delete;
    OemAMPERE& operator=(OemAMPERE&&) = delete;

  public:
    /** Constructs OemAMPERE object
     *
     * @param[in] dBusIntf - D-Bus handler
     * @param[in] mctp_fd - fd of MCTP communications socket
     * @param[in] mctp_eid - MCTP EID of remote host firmware
     * @param[in] repo - pointer to BMC's primary PDR repo
     * @param[in] instanceIdDb - pointer to an InstanceIdDb object
     * @param[in] event - sd_event handler
     * @param[in] invoker - invoker handler
     * @param[in] hostPDRHandler - hostPDRHandler handler
     * @param[in] platformHandler - platformHandler handler
     * @param[in] fruHandler - fruHandler handler
     * @param[in] baseHandler - baseHandler handler
     * @param[in] biosHandler - biosHandler handler
     * @param[in] reqHandler - reqHandler handler
     */
    explicit OemAMPERE(
        const pldm::utils::DBusHandler* /* dBusIntf */, int /* mctp_fd */,
        pldm_pdr* /* repo */, pldm::InstanceIdDb& instanceIdDb,
        sdeventplus::Event& event, responder::Invoker& /* invoker */,
        HostPDRHandler* /* hostPDRHandler */,
        responder::platform::Handler* platformHandler,
        responder::fru::Handler* /* fruHandler */,
        responder::base::Handler* /* baseHandler */,
        responder::bios::Handler* /* biosHandler */,
        platform_mc::Manager* /* platformManager */,
        pldm::requester::Handler<pldm::requester::Request>* reqHandler) :
        instanceIdDb(instanceIdDb), event(event),
        platformHandler(platformHandler), reqHandler(reqHandler)
    {
        oemEventManager = std::make_shared<oem_ampere::OemEventManager>(
            this->event, this->reqHandler, this->instanceIdDb);
        createOemEventHandler(oemEventManager);
    }

  private:
    /** @brief Method for creating OemEventManager
     *
     *  This method also assigns the OemEventManager to the below
     *  different handlers.
     */
    void createOemEventHandler(
        std::shared_ptr<oem_ampere::OemEventManager> oemEventManager)
    {
        platformHandler->registerEventHandlers(
            PLDM_SENSOR_EVENT,
            {[&oemEventManager](const pldm_msg* request, size_t payloadLength,
                                uint8_t formatVersion, uint8_t tid,
                                size_t eventDataOffset) {
                return oemEventManager->handleSensorEvent(
                    request, payloadLength, formatVersion, tid,
                    eventDataOffset);
            }});
    }

  private:
    /** @brief reference to an Instance ID database object, used to obtain PLDM
     * instance IDs
     */
    pldm::InstanceIdDb& instanceIdDb;

    /** @brief reference of main event loop of pldmd, primarily used to schedule
     *  work
     */
    sdeventplus::Event& event;

    /** @brief Platform handler*/
    responder::platform::Handler* platformHandler = nullptr;

    /** @brief pointer to the requester class*/
    requester::Handler<requester::Request>* reqHandler = nullptr;

    std::shared_ptr<oem_ampere::OemEventManager> oemEventManager{};
};

} // namespace oem_ampere
} // namespace pldm
