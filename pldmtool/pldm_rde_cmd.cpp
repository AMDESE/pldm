#include "pldm_rde_cmd.hpp"

#include "common/utils.hpp"
#include "pldm_cmd_helper.hpp"

#include <libpldm/rde.h>
#include <libpldm/utils.h>

#include <map>
#include <optional>

namespace pldmtool
{

namespace rde
{

namespace
{

using namespace pldmtool::helper;
using namespace pldm::utils;
using json = nlohmann::json;
std::vector<std::unique_ptr<CommandInterface>> commands;

} // namespace

class NegotiateRedfishParameters : public CommandInterface
{
  public:
    ~NegotiateRedfishParameters() = default;
    NegotiateRedfishParameters() = delete;
    NegotiateRedfishParameters(const NegotiateRedfishParameters&) = delete;
    NegotiateRedfishParameters(NegotiateRedfishParameters&&) = default;
    NegotiateRedfishParameters& operator=(const NegotiateRedfishParameters&) =
        delete;
    NegotiateRedfishParameters& operator=(NegotiateRedfishParameters&&) =
        delete;

    using CommandInterface::CommandInterface;

    explicit NegotiateRedfishParameters(const char* type, const char* name,
                                        CLI::App* app) :
        CommandInterface(type, name, app)
    {
        app->add_option("-c, --concurrency", concurrencySupport,
                        "Max number of concurrent operations")
            ->required();
        app->add_option("-f, --feature", featureSupport.value,
                        "Bitmask representing supported MC operations")
            ->required();
    }

    std::pair<int, std::vector<uint8_t>> createRequestMsg() override
    {
        std::vector<uint8_t> requestMsg(
            sizeof(pldm_msg_hdr) +
            PLDM_RDE_NEGOTIATE_REDFISH_PARAMETERS_REQ_BYTES);
        auto request = new (requestMsg.data()) pldm_msg;

        auto rc = encode_negotiate_redfish_parameters_req(
            instanceId, concurrencySupport, &featureSupport, request);
        return {rc, requestMsg};
    }

    void parseResponseMsg(pldm_msg* responsePtr, size_t payloadLength) override
    {
        uint8_t cc = 0;
        uint8_t deviceConcurrencySupport;
        bitfield8_t deviceCapabilitiesFlags;
        bitfield16_t deviceFeatureSupport;
        uint32_t deviceConfigurationSignature;
        struct pldm_rde_varstring providerName;

        auto rc = decode_negotiate_redfish_parameters_resp(
            responsePtr, payloadLength, &cc, &deviceConcurrencySupport,
            &deviceCapabilitiesFlags, &deviceFeatureSupport,
            &deviceConfigurationSignature, &providerName);
        if (rc != PLDM_SUCCESS || cc != PLDM_SUCCESS)
        {
            std::cerr << "Response Message Error: "
                      << "rc=" << rc << ",cc=" << (int)cc << std::endl;
            return;
        }

        std::stringstream dt;
        ordered_json data;
        dt << providerName.string_data;

        data["DeviceConcurrencySupport"] = deviceConcurrencySupport;
        data["DeviceCapabilitiesFlags"] = deviceCapabilitiesFlags.byte;
        data["DeviceFeatureSupport"] = deviceFeatureSupport.value;
        data["DeviceConfigurationSignature"] = deviceConfigurationSignature;
        data["DeviceProviderName.format"] = providerName.string_format;
        data["DeviceProviderName.length"] = providerName.string_length_bytes;
        data["DeviceProviderName"] = dt.str();

        pldmtool::helper::DisplayInJson(data);
    }

  private:
    uint8_t concurrencySupport;
    bitfield16_t featureSupport;
};

class NegotiateMediumParameters : public CommandInterface
{
  public:
    ~NegotiateMediumParameters() = default;
    NegotiateMediumParameters() = delete;
    NegotiateMediumParameters(const NegotiateMediumParameters&) = delete;
    NegotiateMediumParameters(NegotiateMediumParameters&&) = default;
    NegotiateMediumParameters& operator=(const NegotiateMediumParameters&) =
        delete;
    NegotiateMediumParameters& operator=(NegotiateMediumParameters&&) = delete;

    using CommandInterface::CommandInterface;

    explicit NegotiateMediumParameters(const char* type, const char* name,
                                       CLI::App* app) :
        CommandInterface(type, name, app)
    {
        app->add_option("-t, --transfersize", mcMaximumTransferSize,
                        "Maximum transfer size in bytes")
            ->required();
    }

    std::pair<int, std::vector<uint8_t>> createRequestMsg() override
    {
        std::vector<uint8_t> requestMsg(
            sizeof(pldm_msg_hdr) +
            PLDM_RDE_NEGOTIATE_MEDIUM_PARAMETERS_REQ_BYTES);
        auto request = new (requestMsg.data()) pldm_msg;

        auto rc = encode_negotiate_medium_parameters_req(
            instanceId, mcMaximumTransferSize, request);

        return {rc, requestMsg};
    }

    void parseResponseMsg(pldm_msg* responsePtr, size_t payloadLength) override
    {
        uint8_t decodedCompletionCode;
        uint32_t decodedDeviceMaximumTransferSize;

        auto rc = decode_negotiate_medium_parameters_resp(
            responsePtr, payloadLength, &decodedCompletionCode,
            &decodedDeviceMaximumTransferSize);

        if (rc != PLDM_SUCCESS || decodedCompletionCode != PLDM_SUCCESS)
        {
            std::cerr << "Response Message Error: "
                      << "rc=" << rc << ",cc=" << (int)decodedCompletionCode
                      << std::endl;
            return;
        }

        ordered_json data;
        data["DeviceMaximumTransferChunkSizeBytes"] =
            decodedDeviceMaximumTransferSize;

        pldmtool::helper::DisplayInJson(data);
    }

  private:
    uint32_t mcMaximumTransferSize;
};

class GetSchemaDictionary : public CommandInterface
{
  public:
    ~GetSchemaDictionary() = default;
    GetSchemaDictionary() = delete;
    GetSchemaDictionary(const GetSchemaDictionary&) = delete;
    GetSchemaDictionary(GetSchemaDictionary&&) = default;
    GetSchemaDictionary& operator=(const GetSchemaDictionary&) = delete;
    GetSchemaDictionary& operator=(GetSchemaDictionary&&) = delete;

    using CommandInterface::CommandInterface;

    explicit GetSchemaDictionary(const char* type, const char* name,
                                 CLI::App* app) :
        CommandInterface(type, name, app)
    {
        app->add_option("-r, --resourceid", resourceID, "Resource ID")
            ->required();
        app->add_option("-s, --schemaclass", schemaClass, "Schema class value")
            ->required();
    }

    std::pair<int, std::vector<uint8_t>> createRequestMsg() override
    {
        std::vector<uint8_t> requestMsg(
            sizeof(pldm_msg_hdr) + PLDM_RDE_SCHEMA_DICTIONARY_REQ_BYTES);
        auto request = new (requestMsg.data()) pldm_msg;

        auto rc = encode_get_schema_dictionary_req(
            instanceId, resourceID, schemaClass,
            PLDM_RDE_SCHEMA_DICTIONARY_REQ_BYTES, request);
        return {rc, requestMsg};
    }

    void parseResponseMsg(pldm_msg* responsePtr, size_t payloadLength) override
    {
        uint8_t decodeCompletionCode;
        uint8_t decodeDictionaryFormat;
        uint32_t decodeTransferHandle;

        auto rc = decode_get_schema_dictionary_resp(
            responsePtr, payloadLength, &decodeCompletionCode,
            &decodeDictionaryFormat, &decodeTransferHandle);
        if (rc != PLDM_SUCCESS || decodeCompletionCode != PLDM_SUCCESS)
        {
            std::cerr << "Response Message Error: "
                      << "rc=" << rc << ",cc=" << (int)decodeCompletionCode
                      << std::endl;
            return;
        }

        ordered_json data;
        data["DictionaryFormat"] = decodeDictionaryFormat;
        data["TransferHandle"] = decodeTransferHandle;

        pldmtool::helper::DisplayInJson(data);
    }

  private:
    uint32_t resourceID;
    uint8_t schemaClass;
};

class GetSchemaURI : public CommandInterface
{
  public:
    ~GetSchemaURI() = default;
    GetSchemaURI() = delete;
    GetSchemaURI(const GetSchemaURI&) = delete;
    GetSchemaURI(GetSchemaURI&&) = default;
    GetSchemaURI& operator=(const GetSchemaURI&) = delete;
    GetSchemaURI& operator=(GetSchemaURI&&) = delete;

    using CommandInterface::CommandInterface;

    explicit GetSchemaURI(const char* type, const char* name, CLI::App* app) :
        CommandInterface(type, name, app)
    {
        app->add_option("-r, --resourceid", resourceID, "Resource ID")
            ->required();
        app->add_option("-s, --schemaclass", schemaClass, "Schema class value")
            ->required();
        app->add_option("-o, --oemextensionnumber", oemExtensionNumber,
                        "OEM extension number")
            ->required();
    }

    std::pair<int, std::vector<uint8_t>> createRequestMsg() override
    {
        std::vector<uint8_t> requestMsg(
            sizeof(pldm_msg_hdr) + PLDM_RDE_SCHEMA_URI_REQ_BYTES);
        auto request = new (requestMsg.data()) pldm_msg;

        auto rc = encode_get_schema_uri_req(instanceId, resourceID, schemaClass,
                                            oemExtensionNumber, request);
        return {rc, requestMsg};
    }

    void parseResponseMsg(pldm_msg* responsePtr, size_t payloadLength) override
    {
        uint8_t decodeCompletionCode;
        uint8_t decodeStringFragmentCount;
        size_t actual_uri_len = 0;
        uint8_t buffer[sizeof(struct pldm_rde_varstring) +
                       PLDM_RDE_SCHEMA_URI_RESP_MAX_VAR_BYTES];
        pldm_rde_varstring* decodeSchemaURI =
            (struct pldm_rde_varstring*)buffer;

        auto rc = decode_get_schema_uri_resp(
            responsePtr, &decodeCompletionCode, &decodeStringFragmentCount,
            decodeSchemaURI, payloadLength, &actual_uri_len);

        if (rc != PLDM_SUCCESS || decodeCompletionCode != PLDM_SUCCESS)
        {
            std::cerr << "Response Message Error: "
                      << "rc=" << rc << ",cc=" << (int)decodeCompletionCode
                      << std::endl;
            return;
        }

        ordered_json data;
        std::stringstream dt;
        dt << decodeSchemaURI->string_data;

        data["StringFragmentCount"] = decodeStringFragmentCount;
        data["SchemaURI.format"] = decodeSchemaURI->string_format;
        data["SchemaURI.length"] = decodeSchemaURI->string_length_bytes;
        data["SchemaURI"] = dt.str();

        pldmtool::helper::DisplayInJson(data);
    }

  private:
    uint32_t resourceID;
    uint8_t schemaClass;
    uint8_t oemExtensionNumber;
};

class GetResourceETag : public CommandInterface
{
  public:
    ~GetResourceETag() = default;
    GetResourceETag() = delete;
    GetResourceETag(const GetResourceETag&) = delete;
    GetResourceETag(GetResourceETag&&) = default;
    GetResourceETag& operator=(const GetResourceETag&) = delete;
    GetResourceETag& operator=(GetResourceETag&&) = delete;

    using CommandInterface::CommandInterface;

    explicit GetResourceETag(const char* type, const char* name,
                             CLI::App* app) : CommandInterface(type, name, app)
    {
        app->add_option("-r, --resourceid", resourceID, "Resource ID")
            ->required();
    }

    std::pair<int, std::vector<uint8_t>> createRequestMsg() override
    {
        std::vector<uint8_t> requestMsg(
            sizeof(pldm_msg_hdr) + PLDM_RDE_GET_RESOURCE_ETAG_REQ_BYTES);
        auto request = new (requestMsg.data()) pldm_msg;

        auto rc = encode_get_resource_etag_req(instanceId, resourceID, request);
        return {rc, requestMsg};
    }

    void parseResponseMsg(pldm_msg* responsePtr, size_t payloadLength) override
    {
        uint8_t decodeCompletionCode;
        const uint32_t etagMaxSize = 1024;

        // TODO: Decide proper size for the buffer
        uint8_t buffer[sizeof(struct pldm_rde_varstring) + etagMaxSize];
        pldm_rde_varstring* decodeEtag = (struct pldm_rde_varstring*)buffer;

        auto rc = decode_get_resource_etag_resp(
            responsePtr, payloadLength, &decodeCompletionCode, decodeEtag);

        if (rc != PLDM_SUCCESS || decodeCompletionCode != PLDM_SUCCESS)
        {
            std::cerr << "Response Message Error: "
                      << "rc=" << rc << ",cc=" << (int)decodeCompletionCode
                      << std::endl;
            return;
        }

        ordered_json data;
        std::stringstream dt;
        dt << decodeEtag->string_data;

        data["ETag.format"] = decodeEtag->string_format;
        data["ETag.length"] = decodeEtag->string_length_bytes;
        data["ETag"] = dt.str();

        pldmtool::helper::DisplayInJson(data);
    }

  private:
    uint32_t resourceID;
};

class RDEMultipartReceive : public CommandInterface
{
  public:
    ~RDEMultipartReceive() = default;
    RDEMultipartReceive() = delete;
    RDEMultipartReceive(const RDEMultipartReceive&) = delete;
    RDEMultipartReceive(RDEMultipartReceive&&) = default;
    RDEMultipartReceive& operator=(const RDEMultipartReceive&) = delete;
    RDEMultipartReceive& operator=(RDEMultipartReceive&&) = delete;

    using CommandInterface::CommandInterface;

    explicit RDEMultipartReceive(const char* type, const char* name,
                                 CLI::App* app) :
        CommandInterface(type, name, app)
    {
        app->add_option("-d, --dataTransferHandle", dataTransferHandle,
                        "Transfer handle ID")
            ->required();

        app->add_option("-o, --operationID", operationID, "Operation ID")
            ->required();

        app->add_option("-t, --transferOperation", transferOperation,
                        "Transfer phase: {0=First, 1=Next, 2=Abort}")
            ->required();
    }

    std::pair<int, std::vector<uint8_t>> createRequestMsg() override
    {
        std::vector<uint8_t> requestMsg(
            sizeof(pldm_msg_hdr) + PLDM_RDE_MULTIPART_RECEIVE_REQ_BYTES);
        auto request = new (requestMsg.data()) pldm_msg;

        auto rc = encode_rde_multipart_receive_req(
            instanceId, dataTransferHandle, operationID, transferOperation,
            request);
        return {rc, requestMsg};
    }

    void parseResponseMsg(pldm_msg* responsePtr, size_t payloadLength) override
    {
        uint8_t decodeCompletionCode;
        uint8_t decodeTransferFlag;
        uint32_t decodeNextDataTransferHandle;
        uint32_t decodeDataLengthBytes;
        uint32_t decodeDataIntegrityChecksum = 0;
        // TODO: Max size to be updated
        constexpr uint32_t rdeMultipartDataMaxSize = 1024;
        uint8_t decodeData[rdeMultipartDataMaxSize];

        auto rc = decode_rde_multipart_receive_resp(
            responsePtr, payloadLength, &decodeCompletionCode,
            &decodeTransferFlag, &decodeNextDataTransferHandle,
            &decodeDataLengthBytes, decodeData, &decodeDataIntegrityChecksum);

        if (rc != PLDM_SUCCESS || decodeCompletionCode != PLDM_SUCCESS)
        {
            std::cerr << "Response Message Error: "
                      << "rc=" << rc << ",cc=" << (int)decodeCompletionCode
                      << std::endl;
            return;
        }
        uint32_t dataOnlyLen = decodeDataLengthBytes;

        if (decodeTransferFlag == PLDM_RDE_END ||
            decodeTransferFlag == PLDM_RDE_START_AND_END)
        {
            dataOnlyLen -= sizeof(decodeDataIntegrityChecksum);
        }
        ordered_json data;
        std::stringstream dt;

        // Format data bytes to display in json
        for (uint32_t i = 0; i < dataOnlyLen; i++)
        {
            dt << " 0x" << std::hex << std::setfill('0') << std::setw(2)
               << static_cast<int>(decodeData[i]);
        }

        data["TransferFlag"] = decodeTransferFlag;
        data["NextDataTransferHandle"] = decodeNextDataTransferHandle;
        data["DataLengthBytes"] = decodeDataLengthBytes;
        data["Data"] = dt.str();
        data["DataIntegrityChecksum"] = decodeDataIntegrityChecksum;

        pldmtool::helper::DisplayInJson(data);
    }

  private:
    uint32_t dataTransferHandle;
    rde_op_id operationID;
    uint8_t transferOperation;
};

class RDEMultipartSend : public CommandInterface
{
  public:
    ~RDEMultipartSend() = default;
    RDEMultipartSend() = delete;
    RDEMultipartSend(const RDEMultipartSend&) = delete;
    RDEMultipartSend(RDEMultipartSend&&) = default;
    RDEMultipartSend& operator=(const RDEMultipartSend&) = delete;
    RDEMultipartSend& operator=(RDEMultipartSend&&) = delete;

    using CommandInterface::CommandInterface;

    explicit RDEMultipartSend(const char* type, const char* name,
                              CLI::App* app) : CommandInterface(type, name, app)
    {
        app->add_option("-t, --dataTransferHandle", dataTransferHandle,
                        "Transfer handle")
            ->required();

        app->add_option("-o, --operationID", operationID, "Operation ID")
            ->required();

        app->add_option(
               "-f, --transferFlag", transferFlag,
               "Transfer stage: {0=Start, 1=Middle, 2=End, 3=StartAndEnd}")
            ->required();
        app->add_option("-z, --nextDataTransferHandle", nextDataTransferHandle,
                        "Next chunk handle or 0")
            ->required();
        app->add_option("-l, --dataLengthBytes", dataLengthBytes,
                        "Length of data in bytes")
            ->required();
        app->add_option("-d, --data", data, "The current chunk of data bytes")
            ->required();
        app->add_option("-c, --dataIntegrityChecksum", dataIntegrityChecksum,
                        "32-bit CRC")
            ->required();
    }

    std::pair<int, std::vector<uint8_t>> createRequestMsg() override
    {
        std::vector<uint8_t> requestMsg(
            sizeof(pldm_msg_hdr) + PLDM_RDE_MULTIPART_SEND_REQ_FIXED_BYTES +
            dataLengthBytes);
        auto request = new (requestMsg.data()) pldm_msg;

        auto rc = encode_rde_multipart_send_req(
            instanceId, dataTransferHandle, operationID, transferFlag,
            nextDataTransferHandle, dataLengthBytes, data.data(),
            dataIntegrityChecksum, request);
        return {rc, requestMsg};
    }

    void parseResponseMsg(pldm_msg* responsePtr, size_t payloadLength) override
    {
        uint8_t decodedCompletionCode;
        uint8_t decodedtransferOperation;

        auto rc = decode_rde_multipart_send_resp(
            responsePtr, payloadLength, &decodedCompletionCode,
            &decodedtransferOperation);

        if (rc != PLDM_SUCCESS || decodedCompletionCode != PLDM_SUCCESS)
        {
            std::cerr << "Response Message Error: "
                      << "rc=" << rc << ",cc=" << (int)decodedCompletionCode
                      << std::endl;
            return;
        }

        ordered_json jsonData;
        jsonData["TransferOperation"] = decodedtransferOperation;
        pldmtool::helper::DisplayInJson(data);
    }

  private:
    uint32_t dataTransferHandle;
    rde_op_id operationID;
    uint8_t transferFlag;
    uint32_t nextDataTransferHandle;
    uint32_t dataLengthBytes;
    std::vector<uint8_t> data;
    uint32_t dataIntegrityChecksum = 0;
};

class RDEOperationInit : public CommandInterface
{
  public:
    ~RDEOperationInit() = default;
    RDEOperationInit() = delete;
    RDEOperationInit(const RDEOperationInit&) = delete;
    RDEOperationInit(RDEOperationInit&&) = default;
    RDEOperationInit& operator=(const RDEOperationInit&) = delete;
    RDEOperationInit& operator=(RDEOperationInit&&) = delete;

    using CommandInterface::CommandInterface;

    explicit RDEOperationInit(const char* type, const char* name,
                              CLI::App* app) : CommandInterface(type, name, app)
    {
        app->add_option("-r, --resourceid", resourceID, "Resource ID")
            ->required();

        app->add_option("-i, --operationID", operationID, "Operation ID")
            ->required();

        app->add_option("-o, --operationType", operationType,
                        "Type of Redfish Operation")
            ->required();

        app->add_option("-f, --operationFlags", operationFlags.byte,
                        "Flags associated with this Operation")
            ->required();

        app->add_option("-d, --sendDataTransferHandle", sendDataTransferHandle,
                        "Handle for BEJ payload transfer")
            ->required();

        app->add_option("-l, --operationLocatorLength", operationLocatorLength,
                        "Length in bytes of the OperationLocator")
            ->required();

        app->add_option("-z, --requestPayloadLength", requestPayloadLength,
                        "Length in bytes of the request payload")
            ->required();

        app->add_option("-b, --operationLocator", operationLocator,
                        "BEJ locator")
            ->required();

        app->add_option("-p, --requestPayload", requestPayload,
                        "The request payload")
            ->required();
    }

    std::pair<int, std::vector<uint8_t>> createRequestMsg() override
    {
        std::vector<uint8_t> requestMsg(
            sizeof(pldm_msg_hdr) + PLDM_RDE_OPERATION_INIT_REQ_FIXED_BYTES +
            operationLocatorLength + requestPayloadLength);
        auto request = new (requestMsg.data()) pldm_msg;

        auto rc = encode_rde_operation_init_req(
            instanceId, resourceID, operationID, operationType, &operationFlags,
            sendDataTransferHandle, operationLocatorLength,
            requestPayloadLength, operationLocator.data(),
            requestPayload.data(), request);

        return {rc, requestMsg};
    }

    void parseResponseMsg(pldm_msg* responsePtr, size_t payloadLength) override
    {
        uint8_t decodedCompletionCode;
        uint8_t decodedOperationStatus;
        uint8_t decodedCompletionPercentage;
        uint32_t decodedCompletionTimeSeconds;
        bitfield8_t decodedOperationExecutionFlags;
        uint32_t decodedResultTransferHandle;
        bitfield8_t decodedPermissionFlags;
        uint32_t decodedResponsePayloadLength;
        const uint32_t etagMaxSize = 1024;

        // TODO: Decide proper size for the buffer
        uint8_t buffer[sizeof(struct pldm_rde_varstring) + etagMaxSize];
        pldm_rde_varstring* decodedEtag = (struct pldm_rde_varstring*)buffer;

        // TODO: Max size to be updated
        constexpr uint32_t responsePayloadMaxSize = 1024;
        uint8_t decodedResponsePayload[responsePayloadMaxSize];

        auto rc = decode_rde_operation_init_resp(
            responsePtr, payloadLength, &decodedCompletionCode,
            &decodedOperationStatus, &decodedCompletionPercentage,
            &decodedCompletionTimeSeconds, &decodedOperationExecutionFlags,
            &decodedResultTransferHandle, &decodedPermissionFlags,
            &decodedResponsePayloadLength, decodedEtag, decodedResponsePayload);

        if (rc != PLDM_SUCCESS || decodedCompletionCode != PLDM_SUCCESS)
        {
            std::cerr << "Response Message Error: "
                      << "rc=" << rc << ",cc=" << (int)decodedCompletionCode
                      << std::endl;
            return;
        }

        ordered_json jsonData;

        std::stringstream dt;
        dt << decodedEtag->string_data;

        jsonData["OperationStatus"] = decodedOperationStatus;
        jsonData["CompletionPercentage"] = decodedCompletionPercentage;
        jsonData["CompletionTimeSeconds"] = decodedCompletionTimeSeconds;
        jsonData["OperationExecutionFlags"] =
            decodedOperationExecutionFlags.byte;
        jsonData["ResultTransferHandle"] = decodedResultTransferHandle;
        jsonData["PermissionFlags"] = decodedPermissionFlags.byte;
        jsonData["ResponsePayloadLength"] = decodedResponsePayloadLength;
        jsonData["OperationStatus"] = decodedOperationStatus;

        jsonData["ETag.format"] = decodedEtag->string_format;
        jsonData["ETag.length"] = decodedEtag->string_length_bytes;
        jsonData["ETag"] = dt.str();

        std::stringstream p;
        if (decodedResponsePayloadLength > 0)
        {
            for (size_t i = 0; i < decodedResponsePayloadLength; i++)
            {
                p << " 0x" << std::hex << std::setfill('0') << std::setw(2)
                  << static_cast<int>(decodedResponsePayload[i]);
            }
            jsonData["ResponsePayload"] = p.str();
        }
        else
            jsonData["ResponsePayload"] = 0;

        pldmtool::helper::DisplayInJson(jsonData);
    }

  private:
    uint32_t resourceID;
    rde_op_id operationID;
    uint8_t operationType;
    bitfield8_t operationFlags;
    uint32_t sendDataTransferHandle;
    uint8_t operationLocatorLength;
    uint32_t requestPayloadLength;

    std::vector<uint8_t> operationLocator;
    std::vector<uint8_t> requestPayload;
};

class RDEOperationComplete : public CommandInterface
{
  public:
    ~RDEOperationComplete() = default;
    RDEOperationComplete() = delete;
    RDEOperationComplete(const RDEOperationComplete&) = delete;
    RDEOperationComplete(RDEOperationComplete&&) = default;
    RDEOperationComplete& operator=(const RDEOperationComplete&) = delete;
    RDEOperationComplete& operator=(RDEOperationComplete&&) = delete;

    using CommandInterface::CommandInterface;

    explicit RDEOperationComplete(const char* type, const char* name,
                                  CLI::App* app) :
        CommandInterface(type, name, app)
    {
        app->add_option("-r, --resourceid", resourceID, "Resource ID")
            ->required();

        app->add_option("-i, --operationID", operationID, "Operation ID")
            ->required();
    }

    std::pair<int, std::vector<uint8_t>> createRequestMsg() override
    {
        std::vector<uint8_t> requestMsg(
            sizeof(pldm_msg_hdr) + PLDM_RDE_OPERATION_COMPLETE_REQ_BYTES);
        auto request = new (requestMsg.data()) pldm_msg;

        auto rc = encode_rde_operation_complete_req(instanceId, resourceID,
                                                    operationID, request);

        return {rc, requestMsg};
    }

    void parseResponseMsg(pldm_msg* responsePtr, size_t payloadLength) override
    {
        uint8_t decodedCompletionCode;

        auto rc = decode_rde_operation_complete_resp(responsePtr, payloadLength,
                                                     &decodedCompletionCode);

        if (rc != PLDM_SUCCESS || decodedCompletionCode != PLDM_SUCCESS)
        {
            std::cerr << "Response Message Error: "
                      << "rc=" << rc << ",cc=" << (int)decodedCompletionCode
                      << std::endl;
            return;
        }
        ordered_json jsonData;
        jsonData["CompletionCode"] = decodedCompletionCode;

        pldmtool::helper::DisplayInJson(jsonData);
    }

  private:
    uint32_t resourceID;
    rde_op_id operationID;
};

class RDEOperationStatus : public CommandInterface
{
  public:
    ~RDEOperationStatus() = default;
    RDEOperationStatus() = delete;
    RDEOperationStatus(const RDEOperationStatus&) = delete;
    RDEOperationStatus(RDEOperationStatus&&) = default;
    RDEOperationStatus& operator=(const RDEOperationStatus&) = delete;
    RDEOperationStatus& operator=(RDEOperationStatus&&) = delete;

    using CommandInterface::CommandInterface;

    explicit RDEOperationStatus(const char* type, const char* name,
                                CLI::App* app) :
        CommandInterface(type, name, app)
    {
        app->add_option("-r, --resourceid", resourceID, "Resource ID")
            ->required();

        app->add_option("-i, --operationID", operationID, "Operation ID")
            ->required();
    }

    std::pair<int, std::vector<uint8_t>> createRequestMsg() override
    {
        std::vector<uint8_t> requestMsg(
            sizeof(pldm_msg_hdr) + PLDM_RDE_OPERATION_STATUS_REQ_BYTES);
        auto request = new (requestMsg.data()) pldm_msg;

        auto rc = encode_rde_operation_status_req(instanceId, resourceID,
                                                  operationID, request);

        return {rc, requestMsg};
    }

    void parseResponseMsg(pldm_msg* responsePtr, size_t payloadLength) override
    {
        uint8_t decodedCompletionCode;
        uint8_t decodedOperationStatus;
        uint8_t decodedCompletionPercentage;
        uint32_t decodedCompletionTimeSeconds;
        bitfield8_t decodedOperationExecutionFlags;
        uint32_t decodedResultTransferHandle;
        bitfield8_t decodedPermissionFlags;
        uint32_t decodedResponsePayloadLength;
        const uint32_t etagMaxSize = 1024;

        // TODO: Decide proper size for the buffer
        uint8_t buffer[sizeof(struct pldm_rde_varstring) + etagMaxSize];
        pldm_rde_varstring* decodedEtag = (struct pldm_rde_varstring*)buffer;

        // TODO: Max size to be updated
        constexpr uint32_t responsePayloadMaxSize = 1024;
        uint8_t decodedResponsePayload[responsePayloadMaxSize];

        auto rc = decode_rde_operation_init_resp(
            responsePtr, payloadLength, &decodedCompletionCode,
            &decodedOperationStatus, &decodedCompletionPercentage,
            &decodedCompletionTimeSeconds, &decodedOperationExecutionFlags,
            &decodedResultTransferHandle, &decodedPermissionFlags,
            &decodedResponsePayloadLength, decodedEtag, decodedResponsePayload);

        if (rc != PLDM_SUCCESS || decodedCompletionCode != PLDM_SUCCESS)
        {
            std::cerr << "Response Message Error: "
                      << "rc=" << rc << ",cc=" << (int)decodedCompletionCode
                      << std::endl;
            return;
        }

        ordered_json jsonData;

        std::stringstream dt;
        dt << decodedEtag->string_data;

        jsonData["OperationStatus"] = decodedOperationStatus;
        jsonData["CompletionPercentage"] = decodedCompletionPercentage;
        jsonData["CompletionTimeSeconds"] = decodedCompletionTimeSeconds;
        jsonData["OperationExecutionFlags"] =
            decodedOperationExecutionFlags.byte;
        jsonData["ResultTransferHandle"] = decodedResultTransferHandle;
        jsonData["PermissionFlags"] = decodedPermissionFlags.byte;
        jsonData["ResponsePayloadLength"] = decodedResponsePayloadLength;
        jsonData["OperationStatus"] = decodedOperationStatus;

        jsonData["ETag.format"] = decodedEtag->string_format;
        jsonData["ETag.length"] = decodedEtag->string_length_bytes;
        jsonData["ETag"] = dt.str();

        std::stringstream p;
        if (decodedResponsePayloadLength > 0)
        {
            for (size_t i = 0; i < decodedResponsePayloadLength; i++)
            {
                p << " 0x" << std::hex << std::setfill('0') << std::setw(2)
                  << static_cast<int>(decodedResponsePayload[i]);
            }
            jsonData["ResponsePayload"] = p.str();
        }
        else
            jsonData["ResponsePayload"] = 0;

        pldmtool::helper::DisplayInJson(jsonData);
    }

  private:
    uint32_t resourceID;
    rde_op_id operationID;
};

class RDEOperationEnumerate : public CommandInterface
{
  public:
    ~RDEOperationEnumerate() = default;
    RDEOperationEnumerate() = delete;
    RDEOperationEnumerate(const RDEOperationEnumerate&) = delete;
    RDEOperationEnumerate(RDEOperationEnumerate&&) = default;
    RDEOperationEnumerate& operator=(const RDEOperationEnumerate&) = delete;
    RDEOperationEnumerate& operator=(RDEOperationEnumerate&&) = delete;

    using CommandInterface::CommandInterface;

    explicit RDEOperationEnumerate(const char* type, const char* name,
                                   CLI::App* app) :
        CommandInterface(type, name, app)
    {}

    std::pair<int, std::vector<uint8_t>> createRequestMsg() override
    {
        std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr));
        auto request = new (requestMsg.data()) pldm_msg;

        auto rc = encode_rde_operation_enumerate_req(instanceId, request);

        return {rc, requestMsg};
    }

    void parseResponseMsg(pldm_msg* responsePtr, size_t payloadLength) override
    {
        uint8_t decodedCompletionCode;
        uint16_t decodedOperationCount;
        // TODO: Decide proper size for the buffer
        const uint32_t maxOperations = 100;

        uint8_t buffer[sizeof(struct pldm_rde_varstring) * maxOperations];
        struct pldm_rde_op_entry* decodedOperations =
            (struct pldm_rde_op_entry*)buffer;

        auto rc = decode_rde_operation_enumerate_resp(
            responsePtr, payloadLength, &decodedCompletionCode,
            &decodedOperationCount, decodedOperations);

        if (rc != PLDM_SUCCESS || decodedCompletionCode != PLDM_SUCCESS)
        {
            std::cerr << "Response Message Error: "
                      << "rc=" << rc << ",cc=" << (int)decodedCompletionCode
                      << std::endl;
            return;
        }

        ordered_json jsonData;

        jsonData["OperationCount"] = decodedOperationCount;

        for (int i = 0; i < decodedOperationCount; i++)
        {
            std::string operationIndex = "[" + std::to_string(i) + "]";
            jsonData["ResourceID" + operationIndex] =
                decodedOperations[i].resource_id;
            jsonData["CompletionPercentage" + operationIndex] =
                decodedOperations[i].operation_id;
            jsonData["CompletionPercentage" + operationIndex] =
                decodedOperations[i].operation_type;
        }

        pldmtool::helper::DisplayInJson(jsonData);
    }
};

class OEMGetResourceInfo : public CommandInterface
{
  public:
    ~OEMGetResourceInfo() = default;
    OEMGetResourceInfo() = delete;
    OEMGetResourceInfo(const OEMGetResourceInfo&) = delete;
    OEMGetResourceInfo(OEMGetResourceInfo&&) = default;
    OEMGetResourceInfo& operator=(const OEMGetResourceInfo&) = delete;
    OEMGetResourceInfo& operator=(OEMGetResourceInfo&&) = delete;

    using CommandInterface::CommandInterface;

    explicit OEMGetResourceInfo(const char* type, const char* name,
                                CLI::App* app) :
        CommandInterface(type, name, app)
    {
        app->description(
            "This command has dependency with rde type 22 PDR; Before running this command please run"
            "'pldmtool platform GetPDR -m <eid> -t redfishresource'");
    }

    void printResourceInfoVect(const std::vector<ResourceInfo>& resourceList)
    {
        for (const auto& res : resourceList)
        {
            ordered_json output;
            output["URI"] = res.uri;
            output["SchemaClass"] = res.schemaClass;
            output["SchemaName"] = res.schemaName;
            output["SchemaVersion"] = res.schemaVersion;
            output["Operations"] = res.operations;
            output["ResourceID"] = res.resourceId;

            pldmtool::helper::DisplayInJson(output);
        }
    }

    void writeJsonArrayToFile(const std::vector<json>& jsonArray)
    {
        std::ofstream outFile(fileName);
        if (!outFile.is_open())
        {
            std::cerr << "Failed to open output file: " << fileName
                      << std::endl;
            return;
        }

        outFile << json(jsonArray).dump(4); // Pretty print
        outFile.close();
    }

    std::vector<json> parseJsonObjectsFromFile()
    {
        std::ifstream file(fileName);
        if (!file.is_open())
        {
            std::cerr << "Failed to open file: " << fileName << std::endl;
            return {};
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        std::vector<json> jsonObjects;
        size_t pos = 0;

        while ((pos = content.find('{')) != std::string::npos)
        {
            size_t end = content.find('}', pos);
            if (end == std::string::npos)
                break;

            std::string objStr = content.substr(pos, end - pos + 1);
            try
            {
                json obj = json::parse(objStr);
                jsonObjects.push_back(obj);
            }
            catch (const json::parse_error& e)
            {
                std::cerr << "Skipping invalid JSON object: " << e.what()
                          << std::endl;
            }

            content = content.substr(end + 1);
        }

        return jsonObjects;
    }

    std::vector<std::shared_ptr<pldm_redfish_resource_pdr>>
        getResourcePdrsFromFile()
    {
        std::ifstream file(fileName);
        json j;
        file >> j;

        std::vector<std::shared_ptr<pldm_redfish_resource_pdr>> pdrList;

        for (const auto& item : j)
        {
            auto pdr = std::make_shared<pldm_redfish_resource_pdr>();

            // Header
            pdr->hdr.version = item["PDRHeaderVersion"];
            pdr->hdr.type = 0x16; // Redfish Resource PDR(PDR type 22)
            pdr->hdr.length = item["dataLength"];

            // Basic fields
            pdr->resource_id = item["ResourceID"];
            pdr->resource_flags.byte = item["ResourceFlags"];
            pdr->cont_resrc_id = item["ContainingResourceID"];

            // ProposedContainingResourceName
            std::string propName = item["ProposedContainingResourceName"];
            pdr->prop_cont_resrc_length =
                static_cast<uint16_t>(propName.size());
            pdr->prop_cont_resrc_name = new uint8_t[propName.size()];
            memcpy(pdr->prop_cont_resrc_name, propName.c_str(),
                   propName.size());

            // SubURI
            std::string subUri = item["SubURI"];
            pdr->sub_uri_length = static_cast<uint16_t>(subUri.size());
            pdr->sub_uri_name = new uint8_t[subUri.size()];
            memcpy(pdr->sub_uri_name, subUri.c_str(), subUri.size());

            // Additional Resources
            pdr->add_resrc_id_count = item["AdditionalResourceIDCount"];
            if (pdr->add_resrc_id_count)
            {
                pdr->additional_resrc =
                    new add_resrc_t*[pdr->add_resrc_id_count];
            }
            else
            {
                pdr->additional_resrc = nullptr;
            }

            for (uint16_t i = 0; i < pdr->add_resrc_id_count; ++i)
            {
                auto resrc = new add_resrc_t();
                std::string idKey =
                    "AdditionalResourceID[" + std::to_string(i) + "]";
                std::string uriKey =
                    "AdditionalResourceSubURI[" + std::to_string(i) + "]";

                resrc->resrc_id = item[idKey];
                std::string uri = item[uriKey];
                resrc->length = static_cast<uint16_t>(uri.size());
                resrc->name = new uint8_t[uri.size()];
                memcpy(resrc->name, uri.c_str(), uri.size());

                pdr->additional_resrc[i] = resrc;
            }

            // Schema Version
            std::string version = item["MajorSchemaVersion"];
            sscanf(version.c_str(), "%hhu.%hhu.%hhu",
                   &pdr->major_schema_version.major,
                   &pdr->major_schema_version.minor,
                   &pdr->major_schema_version.update);
            pdr->major_schema_version.alpha = 0;

            pdr->major_schema_dict_length_bytes =
                item["MajorSchemaDictionaryLengthBytes"];
            pdr->major_schema_dict_signature =
                item["MajorSchemaDictionarySignature"];

            // Major Schema Name
            std::string schemaName = item["MajorSchemaName"];
            pdr->major_schema.length = schemaName.size();
            pdr->major_schema.name = new uint8_t[schemaName.size()];
            memcpy(pdr->major_schema.name, schemaName.c_str(),
                   schemaName.size());

            // OEM Info
            pdr->oem_count = item["OEMCount"];
            if (pdr->oem_count > 0)
            {
                pdr->oem_list = new oem_info_t*[pdr->oem_count];
            }
            else
            {
                pdr->oem_list = nullptr;
            }
            for (uint16_t i = 0; i < pdr->oem_count; ++i)
            {
                auto oem = new oem_info_t();
                std::string oemKey = "OEMName[" + std::to_string(i) + "]";
                std::string oemName = item[oemKey];
                oem->length = static_cast<uint16_t>(oemName.size());
                oem->name = new uint8_t[oemName.size()];
                memcpy(oem->name, oemName.c_str(), oemName.size());
                pdr->oem_list[i] = oem;
            }

            pdrList.push_back(pdr);
        }

        return pdrList;
    }

    void freePdr(std::shared_ptr<pldm_redfish_resource_pdr>& pdr)
    {
        if (!pdr)
            return;

        // Free ProposedContainingResourceName
        delete[] pdr->prop_cont_resrc_name;
        pdr->prop_cont_resrc_name = nullptr;

        // Free SubURI
        delete[] pdr->sub_uri_name;
        pdr->sub_uri_name = nullptr;

        // Free Additional Resources
        if (pdr->additional_resrc)
        {
            for (uint16_t i = 0; i < pdr->add_resrc_id_count; ++i)
            {
                if (pdr->additional_resrc[i])
                {
                    delete[] pdr->additional_resrc[i]->name;
                    delete pdr->additional_resrc[i];
                }
            }
            delete[] pdr->additional_resrc;
            pdr->additional_resrc = nullptr;
        }

        // Free Major Schema Name
        delete[] pdr->major_schema.name;
        pdr->major_schema.name = nullptr;

        // Free OEM list if used
        if (pdr->oem_list)
        {
            for (uint16_t i = 0; i < pdr->oem_count; ++i)
            {
                if (pdr->oem_list[i])
                {
                    delete[] pdr->oem_list[i]->name;
                    delete pdr->oem_list[i];
                }
            }
            delete[] pdr->oem_list;
            pdr->oem_list = nullptr;
        }
    }

    void freePdrList(
        std::vector<std::shared_ptr<pldm_redfish_resource_pdr>>& pdrList)
    {
        for (auto& pdr : pdrList)
        {
            freePdr(pdr);
        }
        pdrList.clear();
    }

    std::pair<int, std::vector<uint8_t>> createRequestMsg() override
    {
        // Converting rde resource pdr cmd output to jason format and store to
        // file.
        std::vector<json> jsonObjects = parseJsonObjectsFromFile();
        writeJsonArrayToFile(jsonObjects);

        std::vector<std::shared_ptr<pldm_redfish_resource_pdr>> pdrList =
            getResourcePdrsFromFile();

        std::vector<ResourceInfo> resourceInfoVect =
            parseRedfishResourcePDRs(pdrList);

        printResourceInfoVect(resourceInfoVect);
        freePdrList(pdrList);

        return {PLDM_ERROR, {}};
    }

    void parseResponseMsg(pldm_msg* responsePtr, size_t payloadLength) override
    {
        (void)responsePtr;
        (void)payloadLength;
        // Function intentionally left blank
    }

  private:
    std::string fileName = "/tmp/redfish_resource_pdr_cache.json";
};

void registerCommand(CLI::App& app)
{
    auto rde = app.add_subcommand("rde", "rde type command");
    rde->require_subcommand(1);

    // Add footer with spec version and reference
    rde->footer(
        "Supported RDE Spec Version: DSP0218 v1.2.0\n"
        "Reference: https://www.dmtf.org/sites/default/files/standards/documents/DSP0218_1.2.0.pdf");

    auto negotiateRedfishParameters = rde->add_subcommand(
        "NegotiateRedfishParameters", "Negotiate Redfish Parameters");
    commands.push_back(std::make_unique<NegotiateRedfishParameters>(
        "rde", "NegotiateRedfishParameters", negotiateRedfishParameters));

    auto negotiateMediumParameters = rde->add_subcommand(
        "NegotiateMediumParameters", "Negotiate Medium Parameters");
    commands.push_back(std::make_unique<NegotiateMediumParameters>(
        "rde", "NegotiateMediumParameters", negotiateMediumParameters));

    auto getSchemaDictionary =
        rde->add_subcommand("GetSchemaDictionary", "Get Schema Dictionary");
    commands.push_back(std::make_unique<GetSchemaDictionary>(
        "rde", "GetSchemaDictionary", getSchemaDictionary));

    auto getSchemaURI = rde->add_subcommand("GetSchemaURI", "Get Schema URI");
    commands.push_back(
        std::make_unique<GetSchemaURI>("rde", "GetSchemaURI", getSchemaURI));

    auto getResourceETag =
        rde->add_subcommand("GetResourceETag", "Get Resource ETag");
    commands.push_back(std::make_unique<GetResourceETag>(
        "rde", "GetResourceETag", getResourceETag));

    auto rdeMultipartReceive =
        rde->add_subcommand("RDEMultipartReceive", "RDE Multipart Receive");
    commands.push_back(std::make_unique<RDEMultipartReceive>(
        "rde", "RDEMultipartReceive", rdeMultipartReceive));

    auto rdeMultipartSend =
        rde->add_subcommand("RDEMultipartSend", "RDE Multipart Send");
    commands.push_back(std::make_unique<RDEMultipartSend>(
        "rde", "RDEMultipartSend", rdeMultipartSend));

    auto rdeOperationInit =
        rde->add_subcommand("RDEOperationInit", "RDE Operation Init");
    commands.push_back(std::make_unique<RDEOperationInit>(
        "rde", "RDEOperationInit", rdeOperationInit));

    auto rdeOperationComplete =
        rde->add_subcommand("RDEOperationComplete", "RDE Operation Complete");
    commands.push_back(std::make_unique<RDEOperationComplete>(
        "rde", "RDEOperationComplete", rdeOperationComplete));

    auto rdeOperationStatus =
        rde->add_subcommand("RDEOperationStatus", "RDE Operation Status");
    commands.push_back(std::make_unique<RDEOperationStatus>(
        "rde", "RDEOperationStatus", rdeOperationStatus));

    auto rdeOperationEnumerate =
        rde->add_subcommand("RDEOperationEnumerate", "RDE Operation Enumerate");
    commands.push_back(std::make_unique<RDEOperationEnumerate>(
        "rde", "RDEOperationEnumerate", rdeOperationEnumerate));

    auto oemGetResourceInfo =
        rde->add_subcommand("OEMGetResourceInfo", "OEM Get Resource Info");
    commands.push_back(std::make_unique<OEMGetResourceInfo>(
        "rde", "OEMGetResourceInfo", oemGetResourceInfo));
}

} // namespace rde

} // namespace pldmtool
