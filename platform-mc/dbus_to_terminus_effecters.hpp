#pragma once

#include "common/instance_id.hpp"
#include "common/types.hpp"
#include "common/utils.hpp"
#include "platform-mc/manager.hpp"
#include "requester/handler.hpp"

#include <phosphor-logging/lg2.hpp>

#include <string>
#include <utility>
#include <vector>

PHOSPHOR_LOG2_USING;

namespace pldm
{

namespace host_effecters
{

using DbusChgHostEffecterProps =
    std::map<dbus::Property, pldm::utils::PropertyValue>;

/** @struct State
 *  Contains the state set id and the possible states for
 *  an effecter
 */
struct PossibleState
{
    uint16_t stateSetId;         //!< State set id
    std::vector<uint8_t> states; //!< Possible states
};

/** @struct DBusEffecterMapping
 *  Contains the D-Bus information for an effecter
 */
struct DBusEffecterMapping
{
    pldm::utils::DBusMapping dbusMap;
    std::vector<pldm::utils::PropertyValue>
        propertyValues;  //!< D-Bus property values
    PossibleState state; //!< Corresponding effecter states
};

/** @struct DBusEffecterMapping
 *  Contains the D-Bus information for an effecter
 */
struct DBusNumericEffecterMapping
{
    pldm::utils::DBusMapping dbusMap;
    uint8_t dataSize;     //!< Numeric effecter PDR data size
    double resolution;    //!< Numeric effecter PDR resolution
    double offset;        //!< Numeric effecter PDR offset
    int8_t unitModifier;  //!< Numeric effecter PDR unitModifier
    double propertyValue; //!< D-Bus property values
};

/** @struct EffecterInfo
 *  Contains the effecter information as a whole
 */
struct EffecterInfo
{
    uint8_t mctpEid;             //!< Host mctp eid
    std::string terminusName;    //!< Terminus name
    uint8_t effecterPdrType;     //!< Effecter PDR type state/numeric
    uint16_t containerId;        //!< Container Id for host effecter
    uint16_t entityType;         //!< Entity type for the host effecter
    uint16_t entityInstance;     //!< Entity instance for the host effecter
    uint8_t compEffecterCnt;     //!< Composite effecter count
    bool checkHostState;         //!< Check host state before setting effecter
    std::vector<DBusEffecterMapping>
        dbusInfo;                //!< D-Bus information for the effecter id
    std::vector<DBusNumericEffecterMapping>
        dbusNumericEffecterInfo; //!< D-Bus information for the effecter id
};

/** @class HostEffecterParser
 *
 *  @brief This class parses the Host Effecter json file and monitors for the
 *         D-Bus changes for the effecters. Upon change, calls the corresponding
 *         setStateEffecterStates on the host
 */
class HostEffecterParser
{
  public:
    HostEffecterParser() = delete;
    HostEffecterParser(const HostEffecterParser&) = delete;
    HostEffecterParser& operator=(const HostEffecterParser&) = delete;
    HostEffecterParser(HostEffecterParser&&) = delete;
    HostEffecterParser& operator=(HostEffecterParser&&) = delete;
    virtual ~HostEffecterParser() = default;

    /** @brief Constructor to create a HostEffecterParser object.
     *  @param[in] instanceIdDb - PLDM InstanceIdDb object pointer
     *  @param[in] fd - socket fd to communicate to host
     *  @param[in] repo -  PLDM PDR repository
     *  @param[in] dbusHandler - D-bus Handler
     *  @param[in] jsonPath - path for the json file
     *  @param[in] handler - PLDM request handler
     */
    explicit HostEffecterParser(
        pldm::InstanceIdDb* instanceIdDb, int fd, const pldm_pdr* repo,
        pldm::utils::DBusHandler* const dbusHandler,
        const std::string& jsonPath,
        pldm::requester::Handler<pldm::requester::Request>* handler,
        platform_mc::Manager* platformManager) :
        instanceIdDb(instanceIdDb), sockFd(fd), pdrRepo(repo),
        dbusHandler(dbusHandler), handler(handler),
        platformManager(platformManager)
    {
        try
        {
            parseEffecterJson(jsonPath);
        }
        catch (const std::exception& e)
        {
            error(
                "The json file '{PATH}' does not exist or malformed, error - '{ERROR}'",
                "PATH", jsonPath, "ERROR", e);
        }
    }

    /* @brief Parses the host effecter json
     *
     * @param[in] jsonPath - path for the json file
     */
    void parseEffecterJson(const std::string& jsonPath);

    /* @brief Method to take action when the subscribed D-Bus property is
     *        changed
     * @param[in] chProperties - list of properties which have changed
     * @param[in] effecterInfoIndex - index of effecterInfo pointer in
     *                                hostEffecterInfo
     * @param[in] dbusInfoIndex - index on dbusInfo pointer in each effecterInfo
     * @param[in] effecterId - host effecter id
     * @return - none
     */
    void processHostEffecterChangeNotification(
        const DbusChgHostEffecterProps& chProperties, size_t effecterInfoIndex,
        size_t dbusInfoIndex, uint16_t effecterId);

    /* @brief Method to take action when the subscribed D-Bus property is
     *        changed
     * @param[in] chProperties - list of properties which have changed
     * @param[in] effecterInfoIndex - index of effecterInfo pointer in
     *                                hostEffecterInfo
     * @param[in] dbusInfoIndex - index on dbusInfo pointer in each effecterInfo

     * @param[in] effecterId - terminus numeric effecter id
     * @return - none
     */
    void processTerminusNumericEffecterChangeNotification(
        const DbusChgHostEffecterProps& chProperties, size_t effecterInfoIndex,
        size_t dbusInfoIndex, uint16_t effecterId);

    /* @brief Populate the property values in each dbusInfo from the json
     *
     * @param[in] dBusValues - json values
     * @param[out] propertyValues - dbusInfo property values
     * @param[in] propertyType - type of the D-Bus property
     * @return - none
     */
    void populatePropVals(
        const pldm::utils::Json& dBusValues,
        std::vector<pldm::utils::PropertyValue>& propertyValues,
        const std::string& propertyType);

    /* @brief Set a host state effecter
     *
     * @param[in] effecterInfoIndex - index of effecterInfo pointer in
     *                                hostEffecterInfo
     * @param[in] stateField - vector of state fields equal to composite
     *                         effecter count in number
     * @param[in] effecterId - host effecter id
     * @return - PLDM status code
     */
    virtual int setHostStateEffecter(
        size_t effecterInfoIndex,
        std::vector<set_effecter_state_field>& stateField, uint16_t effecterId);

    /* @brief Set a terminus numeric effecter
     *
     * @param[in] effecterInfoIndex - index of effecterInfo pointer in
     *                                hostEffecterInfo
     * @param[in] effecterId - host effecter id
     * @param[in] dataSize - data size
     * @param[in] rawValue - raw value
     * @return - PLDM status code
     */
    virtual int setTerminusNumericEffecter(size_t effecterInfoIndex,
                                           uint16_t effecterId,
                                           uint8_t dataSize, double rawValue);

    /* @brief Fetches the new state value and the index in stateField set which
     *        needs to be set with the new value in the setStateEffecter call
     * @param[in] effecterInfoIndex - index of effecterInfo in hostEffecterInfo
     * @param[in] dbusInfoIndex - index of dbusInfo within effecterInfo
     * @param[in] propertyValue - the changed D-Bus property value
     * @return - the new state value
     */
    uint8_t findNewStateValue(size_t effecterInfoIndex, size_t dbusInfoIndex,
                              const pldm::utils::PropertyValue& propertyValue);

    /* @brief Subscribes for D-Bus property change signal on the specified
     *        object
     *
     * @param[in] objectPath - D-Bus object path to look for
     * @param[in] interface - D-Bus interface
     * @param[in] effecterInfoIndex - index of effecterInfo pointer in
     *                                hostEffecterInfo
     * @param[in] dbusInfoIndex - index of dbusInfo within effecterInfo
     * @param[in] effecterId - host effecter id
     */
    virtual void createHostEffecterMatch(
        const std::string& objectPath, const std::string& interface,
        size_t effecterInfoIndex, size_t dbusInfoIndex, uint16_t effecterId);

    /* @brief Adjust the numeric effecter value base on the effecter
     *        configurations
     *
     * @param[in] value - Raw value
     * @param[in] offset - offset config
     * @param[in] resolution - resolution config
     * @param[in] modify - modify config
     *
     * @return - Value of effecter
     */
    double adjustValue(double value, double offset, double resolution,
                       int8_t modify);

  private:
    /* @brief Verify host On state before configure the host effecters
     *
     * @return - true if host is on and false for others cases
     */
    bool isHostOn(void);

  protected:
    pldm::InstanceIdDb* instanceIdDb; //!< Reference to the InstanceIdDb object
                                      //!< to obtain instance id
    int sockFd;                       //!< Socket fd to send message to host
    const pldm_pdr* pdrRepo;          //!< Reference to PDR repo
    std::vector<EffecterInfo> hostEffecterInfo; //!< Parsed effecter information
    std::vector<std::unique_ptr<sdbusplus::bus::match_t>>
        effecterInfoMatch; //!< vector to catch the D-Bus property change
                           //!< signals for the effecters
    const pldm::utils::DBusHandler* dbusHandler; //!< D-bus Handler
    /** @brief PLDM request handler */
    pldm::requester::Handler<pldm::requester::Request>* handler;

    /** @brief MC Platform manager*/
    platform_mc::Manager* platformManager = nullptr;
};

} // namespace host_effecters
} // namespace pldm
