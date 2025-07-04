#pragma once

#include "common/types.hpp"
#include "device_common.hpp"
#include "requester/handler.hpp"

#include <libpldm/base.h>
#include <libpldm/platform.h>
#include <libpldm/rde.h>

#include <phosphor-logging/log.hpp>
#include <requester/handler.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pldm::rde
{

class Device; // forward declare

/**
 * @class DiscoverySession
 * @brief Manages the RDE discovery sequence for a PLDM device
 */
class DiscoverySession
{
  public:
    /**
     * @brief Constructs a DiscoverySession worker using device metadata.
     *
     * This constructor initializes the discovery session by extracting required
     * components—such as the PLDM Instance ID database, request handler, and
     * endpoint identifier—from the provided device context. These parameters
     * are essential for performing protocol-specific discovery operations on
     * the detected device.
     *
     * @param[in] device A reference to the Device object containing discovery
     * dependencies.
     */
    DiscoverySession(Device& device);

    DiscoverySession() = delete;
    DiscoverySession(const DiscoverySession&) = default;
    DiscoverySession& operator=(const DiscoverySession&) = delete;
    DiscoverySession(DiscoverySession&&) = default;
    DiscoverySession& operator=(DiscoverySession&&) = delete;
    ~DiscoverySession() = default;

    /**
     * @brief Initialize the discovery session and prepare command sequence.
     */
    void initialize();

    /**
     * @brief Attempts to update the session's operation state.
     * @param[in] newState The desired OpState
     */
    void updateState(OpState newState);

    /**
     * @brief Returns the current operation state.
     */
    OpState getState() const;

    /**
     * @brief Executes the Redfish negotiation sequence.
     *
     * Initiates the RDE NegotiateRedfishParameters command to establish
     * communication parameters between the management controller and
     * the Redfish-capable device.
     *
     */
    void doNegotiateRedfish();

    /**
     * @brief Handler for Negotiate Redfish Response.
     * @param eid The Endpoint ID.
     * @param respMsg Pointer to the PLDM response message.
     * @param rxLen Length of the received message.
     * @param device parent device pointer.
     */
    void handleNegotiateRedfishResp(uint8_t eid, const pldm_msg* respMsg,
                                    size_t rxLen, Device* device);

  private:
    Device* device_;
    InstanceIdDb& instanceIdDb_;
    pldm::requester::Handler<pldm::requester::Request>* handler_;
    uint8_t eid_;
    uint8_t tid_;
    bool initialized_ = false;
    OpState currentState_ = OpState::Idle;
};

} // namespace pldm::rde
