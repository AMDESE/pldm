#include "operation_session.hpp"

#include "common/instance_id.hpp"
#include "device.hpp"

#include <libpldm/base.h>
#include <libpldm/platform.h>
#include <libpldm/pldm_types.h>
#include <libpldm/rde.h>

#include <phosphor-logging/lg2.hpp>

PHOSPHOR_LOG2_USING;

constexpr uint32_t maxBufferSize = 64 * 1024;
const char *SCHEMA_DICT_FILE = "/home/root/AmdSocConfiguration.dict";

namespace pldm::rde
{

OperationSession::OperationSession(Device& device,
                                   struct OperationInfo op_info) :
    device_(&device), instanceIdDb_(device.getInstanceIdDb()),
    handler_(device.getHandler()), eid_(device.getEid()),
    currentState_(OpState::Idle), op_info(op_info)
{}

void OperationSession::updateState(OpState newState)
{
    currentState_ = newState;
}

OpState OperationSession::getState() const
{
    return currentState_;
}

RedfishPropertyParent* OperationSession::convertJsonToBejTree(const std::string& name, const nlohmann::json& jsonObj)
{
    auto* root = new RedfishPropertyParent();
    std::cout << "Root name " << name << '\n';

    if (name.empty())
        bejTreeInitSet(root, nullptr);  // anonymous
    else
        bejTreeInitSet(root, name.c_str());

    for (auto it = jsonObj.begin(); it != jsonObj.end(); ++it)
    {
        const std::string& key = it.key();
        const auto& value = it.value();

        if (value.is_string())
        {
            auto* leaf = new RedfishPropertyLeafString();
            bejTreeAddString(root, leaf, key.c_str(), value.get<std::string>().c_str());
        }
        else if (value.is_number_integer())
        {
            auto* leaf = new RedfishPropertyLeafInt();
            bejTreeAddInteger(root, leaf, key.c_str(), value.get<int64_t>());
        }
        else if (value.is_boolean())
        {
            auto* leaf = new RedfishPropertyLeafBool();
            bejTreeAddBool(root, leaf, key.c_str(), value.get<bool>());
        }
        else if (value.is_array())
        {
            auto* arrayParent = new RedfishPropertyParent();
            bejTreeInitArray(arrayParent, key.c_str());

            for (const auto& elem : value)
            {
                if (elem.is_string())
                {
                    auto* strElem = new RedfishPropertyLeafString();
                    bejTreeAddString(arrayParent, strElem, "", elem.get<std::string>().c_str());
                }
                else if (elem.is_number_integer())
                {
                    auto* intElem = new RedfishPropertyLeafInt();
                    bejTreeAddInteger(arrayParent, intElem, "", elem.get<int64_t>());
                }
                else if (elem.is_boolean())
                {
                    auto* boolElem = new RedfishPropertyLeafBool();
                    bejTreeAddBool(arrayParent, boolElem, "", elem.get<bool>());
                }
                else if (elem.is_object())
                {
                    auto* child = convertJsonToBejTree("", elem); // anonymous set
                    bejTreeLinkChildToParent(arrayParent, child);
                }
                // handle other types if needed
            }

            bejTreeLinkChildToParent(root, arrayParent);
        }
        else if (value.is_object())
        {
            auto* child = convertJsonToBejTree(key, value);
            bejTreeLinkChildToParent(root, child);
        }
        else
        {
             std::cout << "Invalid type detected in BEJ Tree " << value << "\n";
        }
        // handle null or other types if needed
    }

    return root;
}

std::streamsize OperationSession::readBinaryFile(const char* fileName, std::span<uint8_t> buffer)
{
    std::ifstream inputStream(fileName, std::ios::binary);
    if (!inputStream.is_open())
    {
        std::cerr << "Cannot open file: " << fileName << "\n";
        return 0;
    }
    auto readLength = inputStream.readsome(
        reinterpret_cast<char*>(buffer.data()), buffer.size_bytes());
    if (inputStream.peek() != EOF)
    {
        std::cerr << "Failed to read the complete file: " << fileName
                  << "  read length: " << readLength << "\n";
        return 0;
    }
    return readLength;
}

std::string OperationSession::getRootObjectName(const nlohmann::json& json)
{
    if (json.contains("@odata.type"))
    {
        std::string odataType = json["@odata.type"];
        auto hashPos = odataType.find('#');
        auto dotPos = odataType.find('.', hashPos + 1);
        if (hashPos != std::string::npos && dotPos != std::string::npos)
        {
            return odataType.substr(hashPos + 1, dotPos - hashPos - 1); // e.g., "AmdSocConfiguration"
        }
    }
    return "Root";  // fallback
}

void OperationSession::doOperationInit()
{
    auto instanceId = instanceIdDb_.next(eid_);
    if (!instanceId)
    {
        return;
    }

    int rc = 0;
    uint32_t resourceID = 650; //TODO: need to replace with URI/resource ID API
    rde_op_id operationID = op_info.operationID;
    uint32_t sendDataTransferHandle;
    uint8_t operationLocatorLength;
    uint32_t requestPayloadLength;
    bitfield8_t operationFlags = {0};
    //operationFlags.byte = 0;
    std::vector<uint8_t> operationLocator{0};
    std::vector<uint8_t> requestPayload{0};
    nlohmann::json redfishReqJsonPayload;


    if (op_info.operationType == OperationType::READ)
    {
        sendDataTransferHandle = 0;
        operationLocatorLength = 0;
        requestPayloadLength = 0;
    }
    else if (op_info.operationType == OperationType::UPDATE)
    {
        // TODO: WIP need to add PATCH logic here
	operationFlags.bits.bit1 = 1;
	sendDataTransferHandle = 123; //Do we need to generate random?
        operationLocatorLength = 0;

        try
        {
            redfishReqJsonPayload = nlohmann::json::parse(op_info.payload);
        }
        catch (const nlohmann::json::parse_error& e)
        {
            error("JSON parsing error: EID '{EID}', json payload '{PAYLOAD}", "EID",
                  eid_, "PAYLOAD", op_info.payload);

            updateState(OpState::OperationFailed);
            instanceIdDb_.free(eid_, instanceId);
            return;
        } 

	// TODO: Need to replace with dictionary class API
        std::vector<uint8_t> dictbuffer(maxBufferSize);
        std::span<uint8_t> schemaDictBuffer(dictbuffer);

	if (readBinaryFile(SCHEMA_DICT_FILE,
                       schemaDictBuffer) == 0)
        {
            error("Failed to read schema dictionary file");
            return;
        }

        const Dictionary* annot_dict = device_->dictionaryManager_->getAnnotationDictionary();
        std::span<const uint8_t> annotDictBuffer = annot_dict->getDictionaryBytes();

        BejDictionaries dictionaries = {
            .schemaDictionary = schemaDictBuffer.data(),
	    .schemaDictionarySize = (uint32_t)schemaDictBuffer.size_bytes(),
            .annotationDictionary = annotDictBuffer.data(),
	    .annotationDictionarySize = (uint32_t)annotDictBuffer.size_bytes(),
            .errorDictionary = nullptr,
	    .errorDictionarySize = 0,
        };

        libbej::BejEncoderJson encoder;
        rc = encoder.encode(&dictionaries, bejMajorSchemaClass,
                   convertJsonToBejTree(getRootObjectName(redfishReqJsonPayload), redfishReqJsonPayload));

	requestPayload = encoder.getOutput();
	requestPayloadLength = requestPayload.size();
    }

    Request request(
        sizeof(pldm_msg_hdr) + PLDM_RDE_OPERATION_INIT_REQ_FIXED_BYTES +
        operationLocatorLength + requestPayloadLength);
    auto requestMsg = new (request.data()) pldm_msg;

    rc = encode_rde_operation_init_req(
        instanceId, resourceID, operationID,
        static_cast<uint8_t>(op_info.operationType), &operationFlags,
        sendDataTransferHandle, operationLocatorLength, requestPayloadLength,
        operationLocator.data(), requestPayload.data(), requestMsg);
    if (rc != PLDM_SUCCESS)
    {
        error("encode OperationInit request EID '{EID}', RC '{RC}'", "EID",
              eid_, "RC", rc);

        updateState(OpState::OperationFailed);
        instanceIdDb_.free(eid_, instanceId);
        return;
    }

    rc = handler_->registerRequest(
        eid_, instanceId, PLDM_RDE, PLDM_RDE_OPERATION_INIT, std::move(request),
        [this](uint8_t eid, const pldm_msg* respMsg, size_t rxLen) {
            this->handleOperationInitResp(eid, respMsg, rxLen);
        });

    if (rc)
    {
        error("Failed to send request OperationInit EID '{EID}', RC '{RC}'",
              "EID", eid_, "RC", rc);

        instanceIdDb_.free(eid_, instanceId);

        throw std::runtime_error("Failed to send request OperationInit");
    }

    info("OperationInit Request Waiting");
}

void OperationSession::handleOperationInitResp(
    uint8_t eid, const pldm_msg* respMsg, size_t rxLen)
{
    if (currentState_ == OpState::TimedOut ||
        currentState_ == OpState::Cancelled)
    {
        info("Late response received. Ignoring callback safely. EID={EID}",
             "EID", eid);
        return;
    }

    info("handleOperationInitResp Start: EID={EID} rxLen={RXLEN}", "EID", eid,
         "RXLEN", rxLen);

    if (respMsg == nullptr)
    {
        error("Null PLDM response received from endpoint ID {EID}", "EID", eid);
        updateState(OpState::OperationFailed);
        return;
    }

    // If rxLen is 0, apply fallback — but only use safely capped range
    if (rxLen == 0)
    {
        error("rxLen is 0; applying fallback length. EID={EID}", "EID", eid);
        updateState(OpState::OperationFailed);
        return;
    }

    uint8_t cc = 0;
    uint8_t operationStatus;
    uint8_t completionPercentage;
    uint32_t completionTimeSeconds;
    bitfield8_t operationExecutionFlags;
    uint32_t resultTransferHandle;
    bitfield8_t permissionFlags;
    uint32_t responsePayloadLength;
    const uint32_t etagMaxSize = 1024;

    uint8_t buffer[sizeof(struct pldm_rde_varstring) + etagMaxSize];
    pldm_rde_varstring* etag = (struct pldm_rde_varstring*)buffer;

    uint8_t responsePayload[maxBufferSize];

    auto rc = decode_rde_operation_init_resp(
        respMsg, rxLen, &cc, &operationStatus,
        &completionPercentage, &completionTimeSeconds,
        &operationExecutionFlags, &resultTransferHandle,
        &permissionFlags, &responsePayloadLength, etag,
        responsePayload);

    if (rc != PLDM_SUCCESS || cc != PLDM_SUCCESS)
    {
        error(
            "Failed to decode handleOperationInitResp response rc:{RC} cc:{CC}",
            "RC", rc, "CC", cc);
        updateState(OpState::OperationFailed);
        return;
    }


    std::vector<uint8_t> dictbuffer(maxBufferSize);
    std::span<uint8_t> schemaDictBuffer(dictbuffer);
    if (readBinaryFile(SCHEMA_DICT_FILE,
                       schemaDictBuffer) == 0)

    {
        return;
    }

    const Dictionary* annot_dict = device_->dictionaryManager_->getAnnotationDictionary();
    std::span<const uint8_t> annotDictBuffer = annot_dict->getDictionaryBytes();

    BejDictionaries dictionaries = {
        .schemaDictionary = schemaDictBuffer.data(),
        .schemaDictionarySize = (uint32_t)schemaDictBuffer.size_bytes(),
        .annotationDictionary = annotDictBuffer.data(),
        .annotationDictionarySize = (uint32_t)annotDictBuffer.size_bytes(),
        .errorDictionary = nullptr,
	.errorDictionarySize = 0,
    };

    libbej::BejDecoderJson decoder;
    rc = decoder.decode(dictionaries,
                       std::span{responsePayload,
                                 responsePayloadLength});
    if (rc != 0)
    {
        std::cout << "BEJ decoding failed.\n";
        error(
            "Failed to decode responsePayload from BEJ to json rc:{RC}",
            "RC", rc);
        updateState(OpState::OperationFailed);
        return;
    }

    std::string decoded = decoder.getOutput();

    doOperationComplete();
    return;
}

void OperationSession::doOperationComplete()
{
    auto instanceId = instanceIdDb_.next(eid_);
    if (!instanceId)
    {
        return;
    }

    uint32_t resourceID = 650;
    operationID = 10; /*Can we implement something similar to instandID DB or
                         just use random num generator?*/

    Request request(
        sizeof(pldm_msg_hdr) + PLDM_RDE_OPERATION_COMPLETE_REQ_BYTES);
    auto requestMsg = new (request.data()) pldm_msg;

    int rc = encode_rde_operation_complete_req(instanceId, resourceID,
                                               operationID, requestMsg);
    if (rc != PLDM_SUCCESS)
    {
        error("encode OperationComplete request EID '{EID}', RC '{RC}'", "EID",
              eid_, "RC", rc);

        updateState(OpState::OperationFailed);
        instanceIdDb_.free(eid_, instanceId);
        return;
    }

    rc = handler_->registerRequest(
        eid_, instanceId, PLDM_RDE, PLDM_RDE_OPERATION_COMPLETE,
        std::move(request),
        [this](uint8_t eid, const pldm_msg* respMsg, size_t rxLen) {
            this->handleOperationCompleteResp(eid, respMsg, rxLen);
        });

    if (rc)
    {
        error(
            "Failed to send request OperationCompelete EID '{EID}', RC '{RC}'",
            "EID", eid_, "RC", rc);

        instanceIdDb_.free(eid_, instanceId);

        throw std::runtime_error("Failed to send request OperationComplete");
    }
}

void OperationSession::handleOperationCompleteResp(
    uint8_t eid, const pldm_msg* respMsg, size_t rxLen)
{
    info("handleOperationCompleteResp");
    if (currentState_ == OpState::TimedOut ||
        currentState_ == OpState::Cancelled)
    {
        info("Late response received. Ignoring callback safely. EID={EID}",
             "EID", eid);
        return;
    }

    info("handleOperationCompleteResp Start: EID={EID} rxLen={RXLEN}", "EID",
         eid, "RXLEN", rxLen);

    if (respMsg == nullptr)
    {
        error("Null PLDM response received from endpoint ID {EID}", "EID", eid);
        updateState(OpState::OperationFailed);
        return;
    }

    // If rxLen is 0, apply fallback — but only use safely capped range
    if (rxLen == 0)
    {
        error("rxLen is 0; applying fallback length. EID={EID}", "EID", eid);
        updateState(OpState::OperationFailed);
        return;
    }

    uint8_t cc;
    auto rc = decode_rde_operation_complete_resp(respMsg, rxLen, &cc);
    if (rc != PLDM_SUCCESS || cc != PLDM_SUCCESS)
    {
        error(
            "Failed to decode handleOperationCompleteResp response rc:{RC} cc:{CC}",
            "RC", rc, "CC", cc);
        updateState(OpState::OperationFailed);
        return;
    }

    info("OperationComplete Command completed*");
}
} // namespace pldm::rde
