#include "platform-mc/effecters/numeric/effecter.hpp"

#include "libpldm/platform.h"

#include "common/utils.hpp"
#include "platform-mc/terminus_manager.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/async/execution.hpp>

#include <cmath>
#include <filesystem>
#include <limits>
#include <regex>

#define PLDM_PLATFORM_SET_NUMERIC_EFFECTER_ENABLE_REQ_BYTES 3

namespace pldm
{
namespace platform_mc
{

double vectorToDouble(const std::vector<uint8_t>& vec,
                      pldm_effecter_data_size sizeEnum)
{
    if (vec.empty())
        return 0.0;

    switch (sizeEnum)
    {
        case PLDM_EFFECTER_DATA_SIZE_UINT8:
            return static_cast<double>(vec[0]);

        case PLDM_EFFECTER_DATA_SIZE_SINT8:
            return static_cast<double>(static_cast<int8_t>(vec[0]));

        case PLDM_EFFECTER_DATA_SIZE_UINT16: {
            uint16_t val;
            std::memcpy(&val, vec.data(), sizeof(val));
            return static_cast<double>(val);
        }

        case PLDM_EFFECTER_DATA_SIZE_SINT16: {
            int16_t val;
            std::memcpy(&val, vec.data(), sizeof(val));
            return static_cast<double>(val);
        }

        case PLDM_EFFECTER_DATA_SIZE_UINT32: {
            uint32_t val;
            std::memcpy(&val, vec.data(), sizeof(val));
            return static_cast<double>(val);
        }

        case PLDM_EFFECTER_DATA_SIZE_SINT32: {
            int32_t val;
            std::memcpy(&val, vec.data(), sizeof(val));
            return static_cast<double>(val);
        }

        default:
            return 0.0;
    }
}

// Helper to convert vector to hex string manually
std::string toHexString(const std::vector<uint8_t>& vec) {
    std::stringstream ss;
    ss << "0x";
    for (auto b : vec) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return ss.str();
}

std::vector<uint8_t> doubleToVector(double val, uint8_t sizeEnum)
{
    std::vector<uint8_t> vec;

    switch (sizeEnum) {
        case PLDM_EFFECTER_DATA_SIZE_UINT8: {
            uint8_t tmp = static_cast<uint8_t>(val);
            vec.assign(reinterpret_cast<uint8_t*>(&tmp), reinterpret_cast<uint8_t*>(&tmp) + 1);
            break;
        }
        case PLDM_EFFECTER_DATA_SIZE_SINT8: {
            int8_t tmp = static_cast<int8_t>(val);
            vec.assign(reinterpret_cast<uint8_t*>(&tmp), reinterpret_cast<uint8_t*>(&tmp) + 1);
            break;
        }
        case PLDM_EFFECTER_DATA_SIZE_UINT16: {
            uint16_t tmp = static_cast<uint16_t>(val);
            vec.resize(2);
            std::memcpy(vec.data(), &tmp, 2);
            break;
        }
        case PLDM_EFFECTER_DATA_SIZE_SINT16: {
            int16_t tmp = static_cast<int16_t>(val);
            vec.resize(2);
            std::memcpy(vec.data(), &tmp, 2);
            break;
        }
        case PLDM_EFFECTER_DATA_SIZE_UINT32: {
            uint32_t tmp = static_cast<uint32_t>(val);
            vec.resize(4);
            std::memcpy(vec.data(), &tmp, 4);
            break;
        }
        case PLDM_EFFECTER_DATA_SIZE_SINT32: {
            int32_t tmp = static_cast<int32_t>(val);
            vec.resize(4);
            std::memcpy(vec.data(), &tmp, 4);
            break;
        }
        default:
            // Fallback for unknown or 64-bit types
            vec.resize(4);
            uint32_t fallback = static_cast<uint32_t>(val);
            std::memcpy(vec.data(), &fallback, 4);
            break;
    }

    return vec;
}

pldm_effecter_data_size dbusToPldm(DataSize size)
{
    switch (size)
    {
        case DataSize::uint8:  return PLDM_EFFECTER_DATA_SIZE_UINT8;
        case DataSize::sint8:  return PLDM_EFFECTER_DATA_SIZE_SINT8;
        case DataSize::uint16: return PLDM_EFFECTER_DATA_SIZE_UINT16;
        case DataSize::sint16: return PLDM_EFFECTER_DATA_SIZE_SINT16;
        case DataSize::uint32: return PLDM_EFFECTER_DATA_SIZE_UINT32;
        case DataSize::sint32: return PLDM_EFFECTER_DATA_SIZE_SINT32;
        case DataSize::real32: return PLDM_EFFECTER_DATA_SIZE_UINT32; // No REAL32 in PLDM
        case DataSize::uint64: return PLDM_EFFECTER_DATA_SIZE_UINT64;
        case DataSize::sint64: return PLDM_EFFECTER_DATA_SIZE_SINT64;
        default:
            throw std::runtime_error("Unsupported DataSize enum");
    }
}

OperationalStates pldmToDbusOperState(pldm_effecter_oper_state pldmState)
{
    switch (pldmState)
    {
        case EFFECTER_OPER_STATE_ENABLED_UPDATEPENDING:
            return OperationalStates::EnabledUpdatePending;
        case EFFECTER_OPER_STATE_ENABLED_NOUPDATEPENDING:
            return OperationalStates::EnabledNoUpdatePending;
        case EFFECTER_OPER_STATE_INITIALIZING:
            return OperationalStates::Initializing;
        case EFFECTER_OPER_STATE_DISABLED:
            return OperationalStates::Disabled;
        case EFFECTER_OPER_STATE_SHUTTINGDOWN:
            return OperationalStates::ShuttingDown;
        case EFFECTER_OPER_STATE_UNAVAILABLE:
            return OperationalStates::Unavailable;
        case EFFECTER_OPER_STATE_STATUSUNKNOWN:
            return OperationalStates::StatusUnknown;
        case EFFECTER_OPER_STATE_INTEST:
            return OperationalStates::InTest;
        case EFFECTER_OPER_STATE_FAILED:
            return OperationalStates::Failed;
        default:
            return OperationalStates::Unavailable;
    }
}

void NumericEffecter::setEffecterUnit(uint8_t baseUnit)
{
    this->baseUnit = baseUnit;
    effecterNameSpace = "/xyz/openbmc_project/controls/";
}

void NumericEffecter::registerInterface(
    std::unique_ptr<pldm::platform_mc::NumericEffecterDbusIntf> intf)
{
    interfaces.emplace_back(std::move(intf));
}

double NumericEffecter::rawToUnit(double value)
{
    double convertedValue = value;
    convertedValue *= std::isnan(resolution) ? 1 : resolution;
    convertedValue += std::isnan(offset) ? 0 : offset;

    return convertedValue;
}

double NumericEffecter::unitToRaw(double value)
{
    if (resolution == 0)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double convertedValue = value;
    convertedValue -= std::isnan(offset) ? 0 : offset;
    convertedValue /= std::isnan(resolution) ? 1 : resolution;

    return convertedValue;
}

double NumericEffecter::unitToBase(double value)
{
    double convertedValue = value;
    convertedValue *= std::pow(10, unitModifier);

    return convertedValue;
}

double NumericEffecter::baseToUnit(double value)
{
    double convertedValue = value;
    convertedValue *= std::pow(10, -unitModifier);

    return convertedValue;
}

NumericEffecter::NumericEffecter(
    const pldm_tid_t tid, std::shared_ptr<pldm_numeric_effecter_value_pdr> pdr,
    const std::string& effecterName, const std::string& associationPath,
    Terminus& terminus, TerminusManager& terminusManager, exec::async_scope& scoperef) :
    name(effecterName), tid(tid), terminus(terminus),
    terminusManager(terminusManager), pdr(pdr), scope(scoperef)
{
    if (!pdr)
    {
        throw std::invalid_argument(std::format(
            "Invalid PDR passed for NumericEffecter: {}", effecterName));
    }

    effecterId = pdr->effecter_id;
    entityInfo = {ContainerID(pdr->container_id), EntityType(pdr->entity_type),
                  pldm::pdr::EntityInstance(pdr->entity_instance)};

    needsUpdate = false;

    dataSize = pdr->effecter_data_size;
    resolution = pdr->resolution;
    offset = pdr->offset;
    unitModifier = pdr->unit_modifier;

    setEffecterUnit(pdr->base_unit);

    path = std::filesystem::path(effecterNameSpace) / effecterName;
    path = std::regex_replace(path, std::regex("[^a-zA-Z0-9_/]+"), "_");

    auto& bus = pldm::utils::DBusHandler::getBus();

    try
    {
        associationDefinitionsIntf =
            std::make_unique<AssociationDefinitionsInft>(bus, path.c_str());

        auto dbusIntf = std::make_unique<pldm::platform_mc::NumericEffecterDbusIntf>(bus, path.c_str(), *this);

        switch (pdr->effecter_data_size)
        {
            case PLDM_EFFECTER_DATA_SIZE_UINT8:
            case PLDM_EFFECTER_DATA_SIZE_SINT8:
                dbusIntf->effecterDataSize(DataSize::uint8);
                break;

            case PLDM_EFFECTER_DATA_SIZE_UINT16:
            case PLDM_EFFECTER_DATA_SIZE_SINT16:
                dbusIntf->effecterDataSize(DataSize::uint16);
                break;

            case PLDM_EFFECTER_DATA_SIZE_UINT32:
            case PLDM_EFFECTER_DATA_SIZE_SINT32:
                dbusIntf->effecterDataSize(DataSize::uint32);
                break;
        }

        dbusIntf->effecterOperationalState(OperationalStates::EnabledNoUpdatePending);
        dbusIntf->effecterId(effecterId);

        this->registerInterface(std::move(dbusIntf));

    }
    catch (const sdbusplus::exception_t& e)
    {
        lg2::error(
            "Failed to create Association interface for numeric effecter {PATH} error - {ERROR}",
            "PATH", path, "ERROR", e);
        throw sdbusplus::xyz::openbmc_project::Common::Error::InvalidArgument();
    }
    associationDefinitionsIntf->associations(
        {{"chassis", reverseAssociation.c_str(), associationPath.c_str()}});

    lg2::info("Created Numeric Effecter {NAME} of {SIZE}.", "NAME", effecterName, "SIZE", pdr->effecter_data_size);
}

void NumericEffecter::updateValue(pldm_effecter_oper_state effecterOperState,
                                  double pendingValue, double presentValue, uint8_t sizeEnum)
{
    // Notify all registered interfaces of the value change
    for (auto& intf : interfaces)
    {
        if (!intf)
        {
            continue;
        }
        try
        {
            intf->handleValueChange(*this, effecterOperState,
                                    rawToBase(pendingValue),
                                    rawToBase(presentValue), sizeEnum);
        }
        catch (const std::exception& e)
        {
            lg2::error("Exception in effecter interface for {NAME}: {ERROR}",
                       "NAME", name, "ERROR", e.what());
        }
    }
}

void NumericEffecter::handleErrGetNumericEffecterValue()
{
    // Notify all registered interfaces of the error
    for (auto& intf : interfaces)
    {
        if (!intf)
        {
            continue;
        }
        try
        {
            intf->handleError(*this);
        }
        catch (const std::exception& e)
        {
            lg2::error(
                "Exception in effecter error interface for {NAME}: {ERROR}",
                "NAME", name, "ERROR", e.what());
        }
    }
}

exec::task<int> NumericEffecter::setNumericEffecterValue(double effecterValue)
{
    // Get the appropriate request size based on PLDM platform version
    // Default to v1.2.0 size (7 bytes)
    size_t maxReqSize = 7;

    auto version = terminus.getSupportedTypeVersion(PLDM_PLATFORM);
    if (version.has_value())
    {
        // v1.2.0 uses 7 bytes, v1.3.0 uses 11 bytes
        if (version->major == 1 && version->minor == 2)
        {
            maxReqSize = 7;
        }
        else if (version->major == 1 && version->minor == 3)
        {
            maxReqSize = 11;
        }
    }

    Request request(sizeof(pldm_msg_hdr) + maxReqSize);
    auto requestMsg = reinterpret_cast<pldm_msg*>(request.data());
    union_effecter_data_size effecterValueRaw;
    size_t payloadLength;
    switch (dataSize)
    {
        case PLDM_EFFECTER_DATA_SIZE_UINT8:
            effecterValueRaw.value_u8 = static_cast<uint8_t>(effecterValue);
            payloadLength = PLDM_SET_NUMERIC_EFFECTER_VALUE_MIN_REQ_BYTES;
            break;
        case PLDM_EFFECTER_DATA_SIZE_SINT8:
            effecterValueRaw.value_s8 = static_cast<int8_t>(effecterValue);
            payloadLength = PLDM_SET_NUMERIC_EFFECTER_VALUE_MIN_REQ_BYTES;
            break;
        case PLDM_EFFECTER_DATA_SIZE_UINT16:
            effecterValueRaw.value_u16 = static_cast<uint16_t>(effecterValue);
            payloadLength = PLDM_SET_NUMERIC_EFFECTER_VALUE_MIN_REQ_BYTES + 1;
            break;
        case PLDM_EFFECTER_DATA_SIZE_SINT16:
            effecterValueRaw.value_s16 = static_cast<int16_t>(effecterValue);
            payloadLength = PLDM_SET_NUMERIC_EFFECTER_VALUE_MIN_REQ_BYTES + 1;
            break;
        case PLDM_EFFECTER_DATA_SIZE_UINT32:
            effecterValueRaw.value_u32 = static_cast<uint32_t>(effecterValue);
            payloadLength = PLDM_SET_NUMERIC_EFFECTER_VALUE_MIN_REQ_BYTES + 3;
            break;
        case PLDM_EFFECTER_DATA_SIZE_SINT32:
        default:
            effecterValueRaw.value_s32 = static_cast<int32_t>(effecterValue);
            payloadLength = PLDM_SET_NUMERIC_EFFECTER_VALUE_MIN_REQ_BYTES + 3;
            break;
    }
    auto rc = encode_set_numeric_effecter_value_req(
        0, effecterId, dataSize, &effecterValueRaw.value_u8, requestMsg,
        payloadLength);
    if (rc)
    {
        lg2::error(
            "encode_set_numeric_effecter_value_req failed, tid={TID}, rc={RC}.",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    const pldm_msg* responseMsg = NULL;
    size_t payloadLen = 0;
    rc = co_await terminusManager.sendRecvPldmMsg(tid, request, &responseMsg,
                                                  &payloadLen);
    if (rc)
    {
        co_return rc;
    }

    uint8_t completionCode = PLDM_SUCCESS;
    rc = decode_set_numeric_effecter_value_resp(responseMsg, payloadLen,
                                                &completionCode);
    if (rc)
    {
        lg2::error(
            "Failed to decode response of SetEffecterValue, tid={TID}, rc={RC}.",
            "TID", tid, "RC", rc);
        co_await getNumericEffecterValue();
        co_return rc;
    }

    if (completionCode != PLDM_SUCCESS)
    {
        lg2::error(
            "Failed to decode response of SetEffecterValue, tid={TID}, cc={CC}.",
            "TID", tid, "CC", completionCode);
        co_await getNumericEffecterValue();
        co_return completionCode;
    }

#ifdef OEM_AMD
    auto it = std::find_if(terminus.numericSensors.begin(), terminus.numericSensors.end(),
                           [this](const auto& sensor) { return sensor->sensorId == this->effecterId; });

    if (it != terminus.numericSensors.end())
    {
        lg2::info("Enable polling for sensor ID: {SID}", "SID", (*it)->sensorId);
        (*it)->updateTime = static_cast<uint64_t>(DEFAULT_SENSOR_UPDATER_INTERVAL * 1000);
    }
#endif

    co_await getNumericEffecterValue();

    co_return completionCode;
}

exec::task<int> NumericEffecter::getNumericEffecterValue()
{
    Request request(
        sizeof(pldm_msg_hdr) + PLDM_GET_NUMERIC_EFFECTER_VALUE_REQ_BYTES);
    auto requestMsg = reinterpret_cast<pldm_msg*>(request.data());
    auto rc = encode_get_numeric_effecter_value_req(0, effecterId, requestMsg);
    if (rc)
    {
        lg2::error(
            "encode_get_numeric_effecter_value_req failed, tid={TID}, rc={RC}.",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    const pldm_msg* responseMsg = NULL;
    size_t payloadLen = 0;
    rc = co_await terminusManager.sendRecvPldmMsg(tid, request, &responseMsg,
                                                  &payloadLen);
    if (rc)
    {
        co_return rc;
    }

    uint8_t completionCode = PLDM_SUCCESS;
    uint8_t effecterDataSize = PLDM_EFFECTER_DATA_SIZE_SINT32;
    uint8_t effecterOperationalState = 0;
    union_effecter_data_size pendingValueRaw;
    union_effecter_data_size presentValueRaw;
    rc = decode_get_numeric_effecter_value_resp(
        responseMsg, payloadLen, &completionCode, &effecterDataSize,
        &effecterOperationalState, reinterpret_cast<uint8_t*>(&pendingValueRaw),
        reinterpret_cast<uint8_t*>(&presentValueRaw));
    if (rc)
    {
        lg2::error(
            "Failed to decode response of getNumericEffecterValue, tid={TID}, effecterId={EFFECTERID}, rc={RC}.",
            "TID", tid, "EFFECTERID", effecterId, "RC", rc);
        handleErrGetNumericEffecterValue();
        co_return rc;
    }

    if (completionCode != PLDM_SUCCESS)
    {
        lg2::error(
            "Failed to decode response of getNumericEffecterValue, tid={TID}, effecterId={EFFECTERID}, cc={CC}.",
            "TID", tid, "EFFECTERID", effecterId, "CC", completionCode);
        handleErrGetNumericEffecterValue();
        co_return completionCode;
    }

    double pendingValue;
    double presentValue;
    switch (effecterDataSize)
    {
        case PLDM_EFFECTER_DATA_SIZE_UINT8:
            pendingValue = static_cast<double>(pendingValueRaw.value_u8);
            presentValue = static_cast<double>(presentValueRaw.value_u8);
            break;
        case PLDM_EFFECTER_DATA_SIZE_SINT8:
            pendingValue = static_cast<double>(pendingValueRaw.value_s8);
            presentValue = static_cast<double>(presentValueRaw.value_s8);
            break;
        case PLDM_EFFECTER_DATA_SIZE_UINT16:
            pendingValue = static_cast<double>(pendingValueRaw.value_u16);
            presentValue = static_cast<double>(presentValueRaw.value_u16);
            break;
        case PLDM_EFFECTER_DATA_SIZE_SINT16:
            pendingValue = static_cast<double>(pendingValueRaw.value_s16);
            presentValue = static_cast<double>(presentValueRaw.value_s16);
            break;
        case PLDM_EFFECTER_DATA_SIZE_UINT32:
            pendingValue = static_cast<double>(pendingValueRaw.value_u32);
            presentValue = static_cast<double>(presentValueRaw.value_u32);
            break;
        case PLDM_EFFECTER_DATA_SIZE_SINT32:
            pendingValue = static_cast<double>(pendingValueRaw.value_s32);
            presentValue = static_cast<double>(presentValueRaw.value_s32);
            break;
        default:
            pendingValue = std::numeric_limits<double>::quiet_NaN();
            presentValue = std::numeric_limits<double>::quiet_NaN();
            break;
    }

    updateValue(static_cast<pldm_effecter_oper_state>(effecterOperationalState),
                pendingValue, presentValue, effecterDataSize);
    co_return completionCode;
}

void NumericEffecter::setNumericEffecterValueAsync(double value)
{
    scope.spawn([this, value]() -> exec::task<void> {
        int rc = co_await setNumericEffecterValue(value);

        if (rc != PLDM_SUCCESS)
        {
            co_return;
        }

        co_return;
    }());
}

void NumericEffecter::getNumericEffecterValueAsync()
{
    scope.spawn([this]() -> exec::task<void> {
        int rc = co_await getNumericEffecterValue();

        if (rc != PLDM_SUCCESS)
        {
            co_return;
        }

        co_return;
    }());
}

NumericEffecterDbusIntf::NumericEffecterDbusIntf(
    sdbusplus::bus_t& bus, const char* path, pldm::platform_mc::NumericEffecter& nE) :
    NumericEffecterInherit(bus, path), nEffecter(nE) {}

std::vector<uint8_t> NumericEffecterDbusIntf::effecterValue() const
{
    return NumericEffecterInherit::effecterValue();
}

std::vector<uint8_t> NumericEffecterDbusIntf::effecterValue(std::vector<uint8_t> bav)
{
    double dv = vectorToDouble(bav, dbusToPldm(effecterDataSize()));
    nEffecter.setNumericEffecterValueAsync(dv);

    return NumericEffecterInherit::effecterValue(bav);
}

std::vector<uint8_t> NumericEffecterDbusIntf::presentValue() const
{
    nEffecter.getNumericEffecterValueAsync();
    return NumericEffecterInherit::presentValue();
}

std::vector<uint8_t> NumericEffecterDbusIntf::presentValue(std::vector<uint8_t> value)
{
    return NumericEffecterInherit::presentValue(value);
}

std::vector<uint8_t> NumericEffecterDbusIntf::pendingValue() const
{
    nEffecter.getNumericEffecterValueAsync();
    return NumericEffecterInherit::pendingValue();
}

std::vector<uint8_t> NumericEffecterDbusIntf::pendingValue(std::vector<uint8_t> value)
{
    return NumericEffecterInherit::pendingValue(value);
}

DataSize NumericEffecterDbusIntf::effecterDataSize() const
{
    return NumericEffecterInherit::effecterDataSize();
}

DataSize NumericEffecterDbusIntf::effecterDataSize(DataSize ds)
{
    return NumericEffecterInherit::effecterDataSize(ds);
}

OperationalStates NumericEffecterDbusIntf::effecterOperationalState() const
{
    return NumericEffecterInherit::effecterOperationalState();
}

OperationalStates NumericEffecterDbusIntf::effecterOperationalState(OperationalStates value)
{
    return NumericEffecterInherit::effecterOperationalState(value);
}

void NumericEffecterDbusIntf::handleValueChange(
    pldm::platform_mc::NumericEffecter& /*nE*/,
    pldm_effecter_oper_state effecterOperState,
    double pendingValue, double presentValue, uint8_t sizeEnum)
{
    auto dbusState = pldmToDbusOperState(effecterOperState);

    NumericEffecterInherit::presentValue(doubleToVector(presentValue, sizeEnum), true);
    NumericEffecterInherit::pendingValue(doubleToVector(pendingValue, sizeEnum), true);
    NumericEffecterInherit::effecterOperationalState(dbusState, true);
}

void NumericEffecterDbusIntf::handleError(pldm::platform_mc::NumericEffecter& /*nE*/)
{
    double nanValue = std::numeric_limits<double>::quiet_NaN();

    std::vector<uint8_t> vec(sizeof(double));
    std::memcpy(vec.data(), &nanValue, sizeof(double));

    NumericEffecterInherit::effecterValue(vec);
}

} // namespace platform_mc
} // namespace pldm
