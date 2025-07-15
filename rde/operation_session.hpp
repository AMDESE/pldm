#pragma once

#include "common/types.hpp"
#include "device_common.hpp"
#include "requester/handler.hpp"

#include "libbej/bej_common.h"
#include "libbej/bej_dictionary.h"
#include "libbej/bej_encoder_json.hpp"
#include "libbej/bej_decoder_json.hpp"

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
 * @class OperationSession
 * @brief Manages the RDE operation sequence for a PLDM device
 */
class OperationSession
{
  public:
    /**
     * @brief Initializes an OperationSession for executing a Redfish exchange.
     *
     * Sets up internal state and context required to manage a full Redfish
     * operation with an RDE device. Uses device-specific information and
     * operation metadata to configure session tracking, protocol state, and
     * message flow control.
     *
     * @param[in] device Reference to the target device involved in the
     * operation.
     * @param[in] op_info Metadata struct containing operation type, URI, and
     * format.
     */
    OperationSession(Device& device, struct OperationInfo op_info);

    OperationSession() = delete;
    OperationSession(const OperationSession&) = default;
    OperationSession& operator=(const OperationSession&) = delete;
    OperationSession(OperationSession&&) = default;
    OperationSession& operator=(OperationSession&&) = delete;
    ~OperationSession() = default;

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
     * @brief Initiates Redfish operation with an RDE-capable device.
     *
     * This function prepares and sends the RDEOperationInit command to start a
     * Redfish operation, establishing the operation context and optionally
     * carrying inline request data. It sets flags indicating whether additional
     * payload or parameters will follow, and may trigger immediate execution
     * based on the operation setup.
     */
    void doOperationInit();

    /**
     * @brief Completes the Redfish operation with the RDE device.
     *
     * Sends the OperationComplete command to signal the end of a Redfish
     * operation, allowing the RDE device to release any resources associated
     * with the session. This marks the final step in the operation lifecycle
     * after all payload transfers are done.
     */
    void doOperationComplete();

    /**
     * @brief Handler for Operation Init Response.
     * @param eid The endpoint ID.
     * @param respMsg Pointer to the PLDM response message.
     * @param rxLen Length of the received message.
     * @param device parent device pointer.
     */
    void handleOperationInitResp(uint8_t eid, const pldm_msg* respMsg,
                                 size_t rxLen);

    /**
     * @brief Handler for Operation Complete Response.
     * @param eid The endpoint ID.
     * @param respMsg Pointer to the PLDM response message.
     * @param rxLen Length of the received message.
     * @param device parent device pointer.
     */
    void handleOperationCompleteResp(uint8_t eid, const pldm_msg* respMsg,
                                     size_t rxLen);

    /**
     * @brief Read dictionary binary file.
     * @param fileName path to file.
     * @param dictBuffer buffer to store dictionary
     */
    std::streamsize readBinaryFile(const char* fileName, std::span<uint8_t> dictBuffer);

    /**
     * @brief Get the root node name in JSON payload.
     * @param json The JSON payload.
     */
    std::string getRootObjectName(const nlohmann::json& json);

    /**
     * @brief Create BEJ Tree for encoding.
     * @param name The root node name.
     * @param jsonObj The JSON payloan.
     */
    RedfishPropertyParent* convertJsonToBejTree(const std::string& name, const nlohmann::json& jsonObj);

  private:
    Device* device_;
    InstanceIdDb& instanceIdDb_;
    pldm::requester::Handler<pldm::requester::Request>* handler_;
    uint8_t eid_;
    bool initialized_ = false;
    OpState currentState_ = OpState::Idle;
    struct OperationInfo op_info;
    rde_op_id operationID = 0;
};

} // namespace pldm::rde
