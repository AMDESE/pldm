#pragma once

#include "device_common.hpp"
#include "discov_session.hpp"
#include "xyz/openbmc_project/RDE/Device/server.hpp"

#include <common/instance_id.hpp>
#include <requester/handler.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server/object.hpp>

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

namespace pldm::rde
{

/**
 * @class Device
 * @brief Represents a Redfish-capable device managed via D-Bus.
 */
class Device :
    public sdbusplus::server::object::object<
        sdbusplus::xyz::openbmc_project::RDE::server::Device>
{
  public:
    /**
     * @brief Constructor
     * @param[in] bus - The D-Bus bus object
     * @param[in] path - The D-Bus object path
     * @param[in] instanceIdDb  Pointer to the instance ID database used for
     *                          PLDM message tracking.
     * @param[in] handler       Pointer to the PLDM request handler for sending
     *                          and receiving messages.

     * @param[in] eid -  MCTP Endpoint ID used in PLDM stack
     * @param[in] tid - Target ID used in PLDM stack.
     * @param[in] uuid - Internal registration identifier.
     */
    Device(sdbusplus::bus::bus& bus, const std::string& path,
           pldm::InstanceIdDb* instanceIdDb,
           pldm::requester::Handler<pldm::requester::Request>* handler,
           const uint8_t eid, uint8_t tid, const std::string& uuid);

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

    /**
     * @brief Access the device metadata.
     * @return Reference to metadata.
     */
    Metadata& getMetadata();

    /**
     * @brief Set the device metadata.
     * @param meta Metadata to assign.
     */
    void setMetadata(const Metadata& meta);

    /**
     * @brief Get metadata field by key.
     *
     * This function retrieves an individual metadata field using its string
     * key. Supported types include: std::string, uint8_t, uint16_t, uint32_t,
     * FeatureSupport, and DeviceCapabilities.
     *
     * @param key Metadata field name.
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
     * @param key Metadata field name.
     * @param value Variant containing the new value to set.
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
     * @brief Returns device endpoint identifier (EID).
     * @return 8-bit unsigned integer EID value
     */
    inline uint8_t getEid() const
    {
        return eid_;
    }

    /**
     * @brief Returns Terminus ID (TID).
     * @return 8-bit unsigned integer TID value
     */
    inline uint8_t getTid() const
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

  private:
    Metadata metaData_;
    pldm::InstanceIdDb* instanceIdDb_ = nullptr;
    pldm::requester::Handler<pldm::requester::Request>* handler_ = nullptr;
    uint8_t eid_;
    uint8_t tid_;
    std::string uuid_;
    DeviceState currentState_;
    std::unique_ptr<DiscoverySession> session_;
};

} // namespace pldm::rde
