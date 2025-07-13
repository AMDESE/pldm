#pragma once

#include "device.hpp"
#include "requester/handler.hpp"
#include "requester/mctp_endpoint_discovery.hpp"
#include "xyz/openbmc_project/RDE/Manager/server.hpp"

#include <common/instance_id.hpp>
#include <requester/handler.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server/object.hpp>

#include <future>
#include <memory>
#include <string>

namespace pldm::rde
{

inline constexpr std::string_view RDEManagerObjectPath{
    "/xyz/openbmc_project/RDE/Manager"};
inline constexpr std::string_view DeviceObjectPath{
    "/xyz/openbmc_project/RDE/Device"};

using ObjectPath = sdbusplus::message::object_path;

/**
 * @struct DeviceContext
 * @brief Represents a Redfish Device Enablement (RDE)-capable device.
 *
 * This structure encapsulates all metadata and identifiers required to manage
 * a device participating in RDE via PLDM over MCTP. It bridges internal PLDM
 * stack representations and external D-Bus interfaces.
 *
 * ### Assumptions and Mapping:
 * - `uuid`: Globally unique identifier used during internal registration.
 * - `eid`: MCTP Endpoint ID, used internally in the PLDM stack.
 * - `tid`: Terminus ID used in PLDM stack, represented as a byte.
 * - `friendlyName`: Human-readable name for UI or logs.
 * - `devicePtr`: Pointer to the actual device object.
 */
struct DeviceContext
{
    std::string uuid;         // Unique device UUID (internal registration)
    uint8_t eid;              // MCTP Endpoint ID (internal PLDM stack)
    uint8_t tid;              // Terminus ID used in PLDM stack
    std::string friendlyName; // Human-readable name for the device
    std::shared_ptr<Device> devicePtr; // Pointer to associated device object
};

/**
 * @class Manager
 * @brief Core RDE Manager class implementing the D-Bus interface for Redfish
 * Device Enablement.
 *
 * The Manager class is responsible for centralized management of within the
 * system. It exposes a D-Bus interface and coordinates various aspects of
 * RDE-capable device handling and communication.
 *
 * Includes:
 *   - Exposing the RDE control interface via D-Bus.
 *   - Tracking discovered RDE-capable devices and their metadata (e.g., UUID,
 *     EID).
 *   - Managing the lifecycle of registered devices, including dynamic
 *     add/remove operations.
 *   - Forwarding Redfish-originated requests to appropriate downstream RDE
 *     targets.
 *   - Providing schema and resource discovery services to host tools or Redfish
 *     clients.
 *
 * This class is implemented as a singleton service bound to a well-known name
 * on the system bus.
 */
class Manager :
    public sdbusplus::server::object::object<
        sdbusplus::xyz::openbmc_project::RDE::server::Manager>,
    public pldm::MctpDiscoveryHandlerIntf
{
  public:
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;
    Manager(Manager&&) = delete;
    Manager& operator=(Manager&&) = delete;

    ~Manager() = default;

    /**
     * @brief Construct an RDE Manager object.
     *
     * Initializes the RDE Manager interface on the specified D-Bus path,
     * enabling support for Redfish Device Enablement operations.
     *
     * @param[in] bus           Reference to the system D-Bus connection.
     * @param[in] instanceIdDb  Pointer to the instance ID database used for
     *                          PLDM message tracking.
     * @param[in] handler       Pointer to the PLDM request handler for sending
     *                          and receiving messages.
     */
    Manager(sdbusplus::bus::bus& bus, pldm::InstanceIdDb* instanceIdDb,
            pldm::requester::Handler<pldm::requester::Request>* handler);

    /**
     * @brief Get the instance ID database pointer.
     *
     * @return Pointer to the instance ID database.
     */
    inline pldm::InstanceIdDb* getInstanceIdDb() const
    {
        return instanceIdDb_;
    }

    /**
     * @brief Get the PLDM request handler pointer.
     *
     * @return Pointer to the PLDM request handler.
     */
    inline pldm::requester::Handler<pldm::requester::Request>* getHandler()
        const
    {
        return handler_;
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

    /**
     * @brief Retrieves a pointer to the DeviceContext by eid.
     * @param eid MCTP Endpoint ID
     * @return Pointer to DeviceContext or nullptr if not found
     */
    DeviceContext* getDeviceContext(uint8_t eid);

    /**
     * @brief Implementation for StartRedfishOperation
     * Begin execution of a Redfish operation on the target device. Returns a
     * D-Bus object path for tracking asynchronous progress through an
     * OperationTask instance.
     *
     * @param[in] operationID - Unique identifier for this operation lifecycle.
     *            This ID must be supplied by the caller and reused for any
     *            related commands (e.g., cancellation, polling). Enables
     *            multi-phase tracking and client-side logical correlation.
     * @param[in] operationType - Type of Redfish operation to perform.
     * @param[in] targetURI - URI of the target Redfish resource or action.
     * @param[in] deviceUUID - Uniquely identifies the target device instance.
     * @param[in] eid - Optional endpoint identifier for MCTP or similar
     *            transport. Omit for protocols that do not use EID or when
     *            resolved internally.
     * @param[in] payload - Redfish payload content, either embedded inline as
     *            a JSON/BEJ string, or provided as an external file path
     *            reference.
     * @param[in] payloadFormat - Specifies whether the payload is embedded
     *            inline or referenced via external file path.
     * @param[in] encodingFormat - Encoding format of the payload. Choose
     * 'JSON' for plain text or 'BEJ' for compact binary. JSON is the default
     * for broader compatibility.
     * @param[in] sessionId - Optional grouping label used to associate
     * multiple operations under a logical client session. Useful for
     * long-running tasks, audit logging, or correlation across coordinated
     * workflows.
     *
     * @return path[sdbusplus::message::object_path] - D-Bus object path for
     *         the xyz.openbmc_project.RDE.OperationTask created for this
     *         operation. This object allows clients to monitor execution
     *         progress, retrieve response metadata, and manage task lifecycle.
     */
    ObjectPath startRedfishOperation(
        uint32_t operationID,
        sdbusplus::common::xyz::openbmc_project::rde::Common::OperationType
            operationType,
        std::string targetURI, std::string deviceUUID, uint8_t eid,
        std::string payload, PayloadFormatType payloadFormat,
        EncodingFormatType encodingFormat, std::string sessionId) override;

    /**
     * @brief Get schema information for a specific device.
     *
     * @param[in] deviceUUID  Identifier of the device.
     * @return Vector of key-value maps representing schema information.
     */
    std::vector<std::map<std::string, std::string>> getDeviceSchemaInfo(
        std::string deviceUUID) override;

    /**
     * @brief Get supported Redfish operations for a specific device.
     *
     * @param[in] deviceUUID  Identifier of the device.
     * @return Vector of supported operation types.
     */
    std::vector<
        sdbusplus::common::xyz::openbmc_project::rde::Common::OperationType>
        getSupportedOperations(std::string deviceUUID) override;

    /** @brief Helper function to invoke registered handlers for
     *         the added MCTP endpoints
     *
     *  @param[in] mctpInfos - information of discovered MCTP endpoints
     */
    void handleMctpEndpoints(const MctpInfos& mctpInfos) override;

    /** @brief Helper function to invoke registered handlers for
     *         the removed MCTP endpoints
     *
     *  @param[in] mctpInfos - information of removed MCTP endpoints
     */
    void handleRemovedMctpEndpoints(const MctpInfos&) override
    {
        return;
    }

    /** @brief Helper function to invoke registered handlers for
     *  updating the availability status of the MCTP endpoint
     *
     *  @param[in] mctpInfo - information of the target endpoint
     *  @param[in] availability - new availability status
     */
    void updateMctpEndpointAvailability(const MctpInfo&, Availability) override
    {
        return;
    }

    /** @brief Get Active EIDs.
     *
     *  @param[in] addr - MCTP address of terminus
     *  @param[in] terminiNames - MCTP terminus name
     */
    std::optional<mctp_eid_t> getActiveEidByName(const std::string&) override
    {
        return std::nullopt;
    }

    /**
     * @brief Create a device D-Bus object associated with PLDM discovery.
     *
     * This method initializes and registers a D-Bus object representing a
     * discovered device, using its endpoint ID (EID), unique identifier (UUID),
     * and type identifier (TID). It is typically invoked during the PLDM
     * discovery session as part of Redfish Device Enablement.
     *
     * @param eid   Endpoint ID of the device as reported during MCTP discovery.
     * @param uuid  Unique identifier string for the device.
     * @param tid   Type ID of the device (PLDM Terminus ID).
     * @param pdrPayloads[in] Vector of raw Redfish Resource PDR payloads,
     *        each PDR as a vector of bytes (std::vector<uint8_t>)
     */
    void createDeviceDbusObject(
        uint8_t eid, const std::string& uuid, pldm_tid_t tid,
        const std::vector<std::vector<uint8_t>>& pdrPayloads);

  private:
    pldm::InstanceIdDb* instanceIdDb_ = nullptr;
    pldm::requester::Handler<pldm::requester::Request>* handler_ = nullptr;
    sdbusplus::bus::bus& bus_;
    std::unordered_map<uint8_t, DeviceContext> eidMap_;
    std::unordered_map<uint8_t, std::unique_ptr<sdbusplus::bus::match_t>>
        signalMatches_;
};

} // namespace pldm::rde
