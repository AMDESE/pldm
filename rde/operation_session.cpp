#include "operation_session.hpp"

#include "libbej/bej_dictionary.h"

#include "common/instance_id.hpp"
#include "device.hpp"
#include "libbej/bej_decoder_json.hpp"
#include "libbej/bej_encoder_json.hpp"

#include <libpldm/pldm_types.h>

#include <phosphor-logging/lg2.hpp>

PHOSPHOR_LOG2_USING;

constexpr uint32_t maxBufferSize = 64 * 1024;
constexpr uint8_t CONTAINS_REQ_PAYLOAD = 1;
constexpr bool RDE_START = 0;
constexpr bool RDE_START_AND_END = 1;

namespace pldm::rde
{

OperationSession::OperationSession(std::shared_ptr<Device> device,
                                   struct OperationInfo oipInfo) :
    device_(std::move(device)), eid_(device_->eid()), tid_(device_->getTid()),
    currentState_(OpState::Idle), oipInfo(oipInfo)
{}

void OperationSession::updateState(OpState newState)
{
    currentState_ = newState;
}

OpState OperationSession::getState() const
{
    return currentState_;
}

void OperationSession::markComplete()
{
    complete = true;
}

bool OperationSession::isComplete() const
{
    return complete;
}

bool OperationSession::addToOperationBytes(std::span<const uint8_t> payload,
                                           bool hasChecksum)
{
    if (hasChecksum && !payload.empty())
    {
        // Remove the last byte (checksum)
        responseBuffer.insert(responseBuffer.end(), payload.begin(),
                              payload.end() - 1);
    }
    else
    {
        responseBuffer.insert(responseBuffer.end(), payload.begin(),
                              payload.end());
    }
    return true;
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
            return odataType.substr(hashPos + 1, dotPos - hashPos - 1);
        }
    }
    return "Root"; // fallback
}

RedfishPropertyParent* OperationSession::createBejTree(
    const std::string& name, const nlohmann::json& jsonObj)
{
    auto* root = new RedfishPropertyParent();

    if (name.empty())
        bejTreeInitSet(root, nullptr);
    else
        bejTreeInitSet(root, name.c_str());

    for (auto it = jsonObj.begin(); it != jsonObj.end(); ++it)
    {
        const std::string& key = it.key();
        const auto& value = it.value();

        if (value.is_string())
        {
            auto* leaf = new RedfishPropertyLeafString();
            bejTreeAddString(root, leaf, key.c_str(),
                             value.get<std::string>().c_str());
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
                    bejTreeAddString(arrayParent, strElem, "",
                                     elem.get<std::string>().c_str());
                }
                else if (elem.is_number_integer())
                {
                    auto* intElem = new RedfishPropertyLeafInt();
                    bejTreeAddInteger(arrayParent, intElem, "",
                                      elem.get<int64_t>());
                }
                else if (elem.is_boolean())
                {
                    auto* boolElem = new RedfishPropertyLeafBool();
                    bejTreeAddBool(arrayParent, boolElem, "", elem.get<bool>());
                }
                else if (elem.is_object())
                {
                    auto* child = createBejTree("", elem);
                    bejTreeLinkChildToParent(arrayParent, child);
                }
                // handle other types if needed
            }

            bejTreeLinkChildToParent(root, arrayParent);
        }
        else if (value.is_object())
        {
            auto* child = createBejTree(key, value);
            bejTreeLinkChildToParent(root, child);
        }
        else
        {
            error("Invalid type detected in BEJ Tree for value:{VAL} ", "VAL",
                  value);
        }
        // handle null or other types if needed
    }

    return root;
}

BejDictionaries OperationSession::getDictionaries()
{
    auto null_deleter = [](const auto*) {};
    std::shared_ptr<const DictionaryManager> dictMgr(
        device_->getDictionaryManager(), null_deleter);
    std::shared_ptr<const Dictionary> schemaDict(
        dictMgr->get(currentResourceId_, bejMajorSchemaClass), null_deleter);
    std::shared_ptr<const Dictionary> annotDict(
        dictMgr->get(currentResourceId_, bejAnnotationSchemaClass),
        null_deleter);
    std::span<const uint8_t> schemaDictBuffer =
        schemaDict->getDictionaryBytes();
    std::span<const uint8_t> annotDictBuffer = annotDict->getDictionaryBytes();

    BejDictionaries dictionaries = {
        .schemaDictionary = schemaDictBuffer.data(),
        .schemaDictionarySize =
            static_cast<uint32_t>(schemaDictBuffer.size_bytes()),
        .annotationDictionary = annotDictBuffer.data(),
        .annotationDictionarySize =
            static_cast<uint32_t>(annotDictBuffer.size_bytes()),
        .errorDictionary = nullptr,
        .errorDictionarySize = 0,
    };

    return dictionaries;
}

std::vector<uint8_t> OperationSession::getBejPayload()
{
    BejDictionaries dictionaries = getDictionaries();
    libbej::BejEncoderJson encoder;
    int rc = encoder.encode(
        &dictionaries, bejMajorSchemaClass,
        createBejTree(getRootObjectName(jsonPayload), jsonPayload));
    if (rc != 0)
    {
        error("Failed to encode requestPayload from JSON to BEJ: rc:{RC}", "RC",
              rc);
        updateState(OpState::OperationFailed);
        return {};
    }

    return encoder.getOutput();
}

std::string OperationSession::getJsonStrPayload()
{
    BejDictionaries dictionaries = getDictionaries();
    libbej::BejDecoderJson decoder;
    int rc =
        decoder.decode(dictionaries, std::span<const uint8_t>(responseBuffer));
    if (rc != 0)
    {
        error("Failed to decode responsePayload from BEJ to JSON: rc:{RC}",
              "RC", rc);
        updateState(OpState::OperationFailed);
        return {};
    }

    return decoder.getOutput();
}

void OperationSession::doOperationInit()
{
    auto instanceId = device_->getInstanceIdDb().next(eid_);

    int rc = 0;
    ResourceRegistry* resourceRegistry = device_->getRegistry();
    const std::string& resourceIdStr =
        resourceRegistry->getResourceIdFromUri(oipInfo.targetURI);
    currentResourceId_ = static_cast<uint32_t>(std::stoul(resourceIdStr));
    rde_op_id operationID = oipInfo.operationID;
    uint8_t operationLocatorLength;
    uint32_t requestPayloadLength;
    bitfield8_t operationFlags = {0};
    std::vector<uint8_t> operationLocator{0};
    std::vector<uint8_t> requestPayload{0};

    if (oipInfo.operationType == OperationType::READ)
    {
        sendDataTransferHandle = 0;
        operationLocatorLength = 0;
        requestPayloadLength = 0;
    }
    else if (oipInfo.operationType == OperationType::UPDATE)
    {
        operationLocatorLength = 0;
        operationFlags.bits.bit1 = CONTAINS_REQ_PAYLOAD;

        try
        {
            jsonPayload = nlohmann::json::parse(oipInfo.payload);
        }
        catch (const nlohmann::json::parse_error& e)
        {
            error(
                "JSON parsing error: EID '{EID}', targetURI '{URI}, json payload '{PAYLOAD}",
                "EID", oipInfo.eid, "URI", oipInfo.targetURI, "PAYLOAD",
                oipInfo.payload);
            updateState(OpState::OperationFailed);
            device_->getInstanceIdDb().free(eid_, instanceId);
            return;
        }

        requestBuffer = getBejPayload();

        const auto& chunkMeta =
            device_->getMetadataField("mcMaxTransferChunkSizeBytes");
        const auto* maxChunkSizePtr = std::get_if<uint32_t>(&chunkMeta);
        if (!maxChunkSizePtr)
        {
            error(
                "RDE:Invalid metadata: 'mcMaxTransferChunkSizeBytes' is missing or malformed. EID={EID}",
                "EID", eid_);
            updateState(OpState::OperationFailed);
            device_->getInstanceIdDb().free(eid_, instanceId);
            return;
        }

        uint32_t mcMaxChunkSize = *maxChunkSizePtr;
        uint32_t maxChunkSize =
            (mcMaxChunkSize -
             (sizeof(pldm_msg_hdr) + PLDM_RDE_OPERATION_INIT_REQ_FIXED_BYTES +
              operationLocatorLength));
        if (requestPayload.size() > maxChunkSize)
        {
            multiPartTransferFlag = true;
            sendDataTransferHandle = instanceId;
            requestPayloadLength = maxChunkSize;
            requestPayload = getChunk(requestPayloadLength, RDE_START);
        }
        else
        {
            sendDataTransferHandle = 0;
            requestPayloadLength =
                (sizeof(pldm_msg_hdr) +
                 PLDM_RDE_OPERATION_INIT_REQ_FIXED_BYTES +
                 operationLocatorLength + requestPayloadLength);
            requestPayload = getChunk(requestPayloadLength, RDE_START_AND_END);
        }
    }

    Request request(
        sizeof(pldm_msg_hdr) + PLDM_RDE_OPERATION_INIT_REQ_FIXED_BYTES +
        operationLocatorLength + requestPayloadLength);
    auto requestMsg = new (request.data()) pldm_msg;

    rc = encode_rde_operation_init_req(
        instanceId, currentResourceId_, operationID,
        static_cast<uint8_t>(oipInfo.operationType), &operationFlags,
        sendDataTransferHandle, operationLocatorLength, requestPayloadLength,
        operationLocator.data(), requestPayload.data(), requestMsg);
    if (rc != PLDM_SUCCESS)
    {
        error("encode OperationInit request EID '{EID}', RC '{RC}'", "EID",
              eid_, "RC", rc);

        updateState(OpState::OperationFailed);
        device_->getInstanceIdDb().free(eid_, instanceId);
        return;
    }

    rc = device_->getHandler()->registerRequest(
        eid_, instanceId, PLDM_RDE, PLDM_RDE_OPERATION_INIT, std::move(request),
        [this](uint8_t /*eid*/, const pldm_msg* respMsg, size_t rxLen) {
            this->handleOperationInitResp(respMsg, rxLen);
        });

    if (rc)
    {
        error("Failed to send request OperationInit EID '{EID}', RC '{RC}'",
              "EID", eid_, "RC", rc);
        device_->getInstanceIdDb().free(eid_, instanceId);
        throw std::runtime_error("Failed to send request OperationInit");
    }

    info("OperationInit Request Waiting");
}

void OperationSession::handleOperationInitResp(const pldm_msg* respMsg,
                                               size_t rxLen)
{
    if (currentState_ == OpState::TimedOut ||
        currentState_ == OpState::Cancelled)
    {
        info("Late response received. Ignoring callback safely. EID={EID}",
             "EID", eid_);
        return;
    }

    info("handleOperationInitResp Start: EID={EID} rxLen={RXLEN}", "EID", eid_,
         "RXLEN", rxLen);

    if (respMsg == nullptr)
    {
        error("Null PLDM response received from endpoint ID {EID}", "EID",
              eid_);
        updateState(OpState::OperationFailed);
        return;
    }

    if (rxLen == 0)
    {
        error("rxLen is 0; applying fallback length. EID={EID}", "EID", eid_);
        updateState(OpState::OperationFailed);
        return;
    }

    ResourceRegistry* resourceRegistry = device_->getRegistry();
    const std::string& resourceIdStr =
        resourceRegistry->getResourceIdFromUri(oipInfo.targetURI);
    currentResourceId_ = static_cast<uint32_t>(std::stoul(resourceIdStr));
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
        respMsg, rxLen, &cc, &operationStatus, &completionPercentage,
        &completionTimeSeconds, &operationExecutionFlags, &resultTransferHandle,
        &permissionFlags, &responsePayloadLength, etag, responsePayload);

    if (rc != PLDM_SUCCESS || cc != PLDM_SUCCESS)
    {
        error(
            "Failed to decode handleOperationInitResp response rc:{RC} cc:{CC}",
            "RC", rc, "CC", cc);
        updateState(OpState::OperationFailed);
        return;
    }

    if (oipInfo.operationType == OperationType::READ)
    {
        if (resultTransferHandle == 0)
            addChunk(currentResourceId_,
                     {responsePayload, responsePayloadLength}, false, true);
        else if (resultTransferHandle != 0)
        {
            try
            {
                addChunk(currentResourceId_,
                         {responsePayload, responsePayloadLength}, false,
                         false);
                info(
                    "RDE: Received transferHandle={HANDLE} for resourceId={RID}",
                    "HANDLE", resultTransferHandle, "RID", currentResourceId_);

                receiver_ = std::make_unique<pldm::rde::MultipartReceiver>(
                    device_, eid_, resultTransferHandle);

                receiver_->start(
                    [this](std::span<const uint8_t> payload,
                           const pldm::rde::MultipartRcvMeta& meta) {
                        addChunk(currentResourceId_, payload, meta.hasChecksum,
                                 meta.isFinalChunk);
                        if (isComplete())
                        {
                            info("MultipartReceive sequence completed");
                        }
                        else
                        {
                            receiver_->setTransferOperation(
                                PLDM_RDE_XFER_NEXT_PART);
                            receiver_->sendReceiveRequest(meta.nextHandle);
                        }
                    },
                    [this]() {
                        info(
                            "RDE: Multipart transfer complete for resourceId={RID}",
                            "RID", currentResourceId_);
                    },
                    [this](const std::string& reason) {
                        error(
                            "RDE: Multipart transfer failed for resourceId={RID}, Error={ERR}",
                            "RID", currentResourceId_, "ERR", reason);
                    });
            }
            catch (const std::exception& ex)
            {
                error(
                    "RDE: Exception during multipart transfer setup for resourceId={RID}, Error={ERR}",
                    "RID", currentResourceId_, "ERR", ex.what());
            }
        }

        std::string decoded = getJsonStrPayload();
    }
    else if (oipInfo.operationType == OperationType::UPDATE)
    {
        // TODO: Add handler for RDEMultipartSend
        if (multiPartTransferFlag)
        {
            try
            {
                info(
                    "RDE: Received transferHandle={HANDLE} for resourceId={RID}",
                    "HANDLE", resultTransferHandle, "RID", currentResourceId_);

                sender_ = std::make_unique<pldm::rde::MultipartSender>(
                    device_, eid_, sendDataTransferHandle, requestBuffer);

                sender_->start(
                    [this](std::span<const uint8_t> payload,
                           const pldm::rde::MultipartSndMeta& meta) {
                        std::cout << "pauload " << payload.data() << "\n";
                        if (isComplete())
                        {
                            info("Multipartsend completed");
                            multiPartTransferFlag = false;
                        }
                        else
                        {
                            sender_->setTransferFlag(PLDM_RDE_START);
                            sender_->sendReceiveRequest(meta.nextHandle);
                        }
                    },
                    [this]() {
                        info(
                            "RDE: Multipart transfer complete for resourceId={RID}",
                            "RID", currentResourceId_);
                    },
                    [this](const std::string& reason) {
                        error(
                            "RDE: Multipart transfer failed for resourceId={RID}, Error={ERR}",
                            "RID", currentResourceId_, "ERR", reason);
                    });
            }
            catch (const std::exception& ex)
            {
                error(
                    "RDE: Exception during multipart transfer setup for resourceId={RID}, Error={ERR}",
                    "RID", currentResourceId_, "ERR", ex.what());
            }
        }
    }

    // TODO: Add D-bus response handler
    doOperationComplete();
    return;
}

void OperationSession::doOperationComplete()
{
    auto instanceId = device_->getInstanceIdDb().next(eid_);

    ResourceRegistry* resourceRegistry = device_->getRegistry();
    const std::string& resourceIdStr =
        resourceRegistry->getResourceIdFromUri(oipInfo.targetURI);
    currentResourceId_ = static_cast<uint32_t>(std::stoul(resourceIdStr));

    operationID = oipInfo.operationID;

    Request request(
        sizeof(pldm_msg_hdr) + PLDM_RDE_OPERATION_COMPLETE_REQ_BYTES);
    auto requestMsg = new (request.data()) pldm_msg;

    int rc = encode_rde_operation_complete_req(instanceId, currentResourceId_,
                                               operationID, requestMsg);
    if (rc != PLDM_SUCCESS)
    {
        error("encode OperationComplete request EID '{EID}', RC '{RC}'", "EID",
              eid_, "RC", rc);

        updateState(OpState::OperationFailed);
        device_->getInstanceIdDb().free(eid_, instanceId);
        return;
    }

    rc = device_->getHandler()->registerRequest(
        eid_, instanceId, PLDM_RDE, PLDM_RDE_OPERATION_COMPLETE,
        std::move(request),
        [this](uint8_t /*eid*/, const pldm_msg* respMsg, size_t rxLen) {
            this->handleOperationCompleteResp(respMsg, rxLen);
        });

    if (rc)
    {
        error(
            "Failed to send request OperationCompelete EID '{EID}', RC '{RC}'",
            "EID", eid_, "RC", rc);

        device_->getInstanceIdDb().free(eid_, instanceId);

        throw std::runtime_error("Failed to send request OperationComplete");
    }
}

void OperationSession::handleOperationCompleteResp(const pldm_msg* respMsg,
                                                   size_t rxLen)
{
    info("handleOperationCompleteResp");
    if (currentState_ == OpState::TimedOut ||
        currentState_ == OpState::Cancelled)
    {
        info("Late response received. Ignoring callback safely. EID={EID}",
             "EID", eid_);
        return;
    }

    info("handleOperationCompleteResp Start: EID={EID} rxLen={RXLEN}", "EID",
         eid_, "RXLEN", rxLen);

    if (respMsg == nullptr)
    {
        error("Null PLDM response received from endpoint ID {EID}", "EID",
              eid_);
        updateState(OpState::OperationFailed);
        return;
    }

    if (rxLen == 0)
    {
        error("rxLen is 0; applying fallback length. EID={EID}", "EID", eid_);
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

    info("OperationComplete done");
}

void OperationSession::addChunk(uint32_t resourceId,
                                std::span<const uint8_t> payload,
                                bool hasChecksum, bool isFinalChunk)
{
    info("RDE: OperationSession addChunk Enter for resourceID={RC}", "RC",
         resourceId);

    if (payload.empty())
    {
        throw std::invalid_argument("Payload chunk is empty.");
    }

    if (!addToOperationBytes(payload, hasChecksum))
    {
        throw std::runtime_error("Failed to add chunk to dictionary.");
    }

    if (isFinalChunk)
        markComplete();

    info("RDE: OperationSession addChunk Exit for resourceID={RC}", "RC",
         currentResourceId_);
}

std::vector<uint8_t> OperationSession::getFromOperationBytes(
    uint32_t requestPayloadLength)
{
    if (requestBuffer.size() < requestPayloadLength)
    {
        throw std::runtime_error(
            "Request buffer too small for requested payload length.");
    }

    // Extract the chunk from the front
    std::vector<uint8_t> chunk(requestBuffer.begin(),
                               requestBuffer.begin() + requestPayloadLength);

    // Remove the extracted bytes from the requestBuffer
    requestBuffer.erase(requestBuffer.begin(),
                        requestBuffer.begin() + requestPayloadLength);

    return chunk;
}

std::vector<uint8_t> OperationSession::getChunk(uint32_t requestPayloadLength,
                                                bool isFinalChunk)
{
    info("RDE: OperationSession getChunk Enter for resourceID={RC}", "RC",
         currentResourceId_);

    auto chunk = getFromOperationBytes(requestPayloadLength);

    if (chunk.empty())
    {
        throw std::invalid_argument("Payload chunk is empty.");
    }

    if (isFinalChunk)
    {
        markComplete();
    }

    info("RDE: OperationSession getChunk Exit for resourceID={RC}", "RC",
         currentResourceId_);

    return chunk;
}

} // namespace pldm::rde
