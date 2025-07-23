#include "device.hpp"
#include "multipart_recv.hpp"

#include <phosphor-logging/lg2.hpp>

#include <cstring>

PHOSPHOR_LOG2_USING;

constexpr bool RDE_MIDDLE = 0;
constexpr bool RDE_END = 1;

namespace pldm::rde
{

MultipartSender::MultipartSender(std::shared_ptr<Device> device, uint8_t eid,
                                 uint32_t transferHandle,
                                 std::span<const uint8_t> dataPayload) :
    device_(std::move(device)), eid_(eid), transferHandle_(transferHandle),
    dataPayload_(dataPayload)
{}

void MultipartSender::start(
    std::function<void(std::span<const uint8_t>, const MultipartSndMeta&)>
        onData,
    std::function<void()> onComplete,
    std::function<void(std::string)> onFailure)
{
    onData_ = std::move(onData);
    onComplete_ = std::move(onComplete);
    onFailure_ = std::move(onFailure);

    sendReceiveRequest(transferHandle_);
}

void MultipartSender::markComplete()
{
    complete = true;
}

void MultipartSender::sendReceiveRequest(uint32_t handle)
{
    uint8_t instanceId = device_->getInstanceIdDb().next(eid_);
    if (instanceId == 0)
    {
        lg2::error("RDE: Instance ID allocation failed: EID={EID}", "EID",
                   eid_);
        if (onFailure_)
            onFailure_("RDE: Instance ID allocation failed");
        return;
    }

    lg2::info("RDE: Allocated Instance ID={ID} for EID={EID}", "ID", instanceId,
              "EID", eid_);

    rde_op_id operationID{};
    // std::span<uint8_t> dataPayload;

    const auto& chunkMeta =
        device_->getMetadataField("mcMaxTransferChunkSizeBytes");
    const auto* maxChunkSizePtr = std::get_if<uint32_t>(&chunkMeta);
    if (!maxChunkSizePtr)
    {
        error(
            "RDE:Invalid metadata: 'mcMaxTransferChunkSizeBytes' is missing or malformed. EID={EID}",
            "EID", eid_);
        return;
    }

    uint32_t mcMaxChunkSize = *maxChunkSizePtr;
    std::vector<uint8_t> data;
    uint32_t dataLengthBytes;
    if (dataPayload_.size() < mcMaxChunkSize)
    {
        dataLengthBytes = dataPayload_.size();
        data = getChunks(dataPayload_.size(), RDE_END);
    }
    else
    {
        dataLengthBytes = mcMaxChunkSize;
        data = getChunks(mcMaxChunkSize, RDE_MIDDLE);
    }
    uint32_t dataIntegrityChecksum = 0; //?

    Request request(sizeof(pldm_msg_hdr) +
                    PLDM_RDE_MULTIPART_SEND_REQ_FIXED_BYTES + dataLengthBytes);
    auto requestMsg = reinterpret_cast<pldm_msg*>(request.data());

    lg2::info(
        "RDE: Sending multipart request: EID={EID}, HANDLE={HANDLE}, INST_ID={ID}, OP={OP}",
        "EID", eid_, "HANDLE", handle, "ID", instanceId, "OP",
        transferOperation_);

    int rc = encode_rde_multipart_send_req(
        instanceId, handle, operationID, transferFlag_, handle, dataLengthBytes,
        data.data(), dataIntegrityChecksum, requestMsg);
    if (rc != PLDM_SUCCESS)
    {
        device_->getInstanceIdDb().free(eid_, instanceId);
        lg2::error("RDE: Request encoding failed: RC={RC}, EID={EID}", "RC", rc,
                   "EID", eid_);
        if (onFailure_)
            onFailure_("RDE: Request encoding failed");
        return;
    }

    rc = device_->getHandler()->registerRequest(
        eid_, instanceId, PLDM_RDE, PLDM_RDE_MULTIPART_SEND, std::move(request),
        [this](uint8_t /*eid*/, const pldm_msg* msg, size_t len) {
            this->handleSendResp(msg, len);
        });
    if (rc)
    {
        device_->getInstanceIdDb().free(eid_, instanceId);
        lg2::error("RDE: Request registration failed: RC={RC}, EID={EID}", "RC",
                   rc, "EID", eid_);
        if (onFailure_)
            onFailure_("RDE: Request registration failed");
    }
}

void MultipartSender::handleSendResp(const pldm_msg* respMsg, size_t rxLen)
{
    if (!respMsg || rxLen == 0)
    {
        lg2::error("RDE: Empty multipart response: EID={EID}, LEN={LEN}", "EID",
                   eid_, "LEN", rxLen);
        if (onFailure_)
            onFailure_("RDE: Empty or invalid response");
        return;
    }

    uint8_t cc = 0;
    uint32_t nextHandle = 0, length = 0, checksum = 0;
    constexpr uint32_t maxSize = 1024;
    uint8_t buffer[maxSize]{};
    uint8_t transferOperation;

    int rc =
        decode_rde_multipart_send_resp(respMsg, rxLen, &cc, &transferOperation);
    if (rc != PLDM_SUCCESS || cc != PLDM_SUCCESS)
    {
        lg2::error("RDE: Chunk decode failed: RC={RC}, CC={CC}, EID={EID}",
                   "RC", rc, "CC", cc, "EID", eid_);
        if (onFailure_)
            onFailure_("Chunk decode failed");
        return;
    }

    if (length > maxSize)
    {
        lg2::error(
            "RDE: Chunk length overflow: LEN={LEN}, MAX={MAX}, EID={EID}",
            "LEN", length, "MAX", maxSize, "EID", eid_);
        if (onFailure_)
            onFailure_("RDE: Chunk length overflow");
        return;
    }

    std::span<const uint8_t> payload(buffer, length);
    const bool isFinalChunk = (transferOperation == PLDM_RDE_XFER_COMPLETE);

    MultipartSndMeta meta{.schemaClass = 0, // can be extended later
                          .hasChecksum = true,
                          .isFinalChunk = isFinalChunk,
                          .checksum = checksum,
                          .length = length};

    lg2::info(
        "RDE: Send multipart chunk: EID={EID}, HANDLE={HANDLE}, LEN={LEN}, FINAL={FINAL}",
        "EID", eid_, "HANDLE", nextHandle, "LEN", length, "FINAL",
        isFinalChunk);

    if (onData_)
    {
        onData_(payload, meta);
    }
}

std::vector<uint8_t> MultipartSender::getFromTransferBytes(
    uint32_t requestPayloadLength)
{
    if (dataPayload_.size() < requestPayloadLength)
    {
        throw std::runtime_error(
            "Request buffer too small for requested payload length.");
    }

    // Extract chunk
    std::vector<uint8_t> chunk(dataPayload_.begin(),
                               dataPayload_.begin() + requestPayloadLength);

    // Move the span forward
    dataPayload_ = dataPayload_.subspan(requestPayloadLength);

    return chunk;
}

std::vector<uint8_t> MultipartSender::getChunks(uint32_t requestPayloadLength,
                                                bool isFinalChunk)
{
    auto chunk = getFromTransferBytes(requestPayloadLength);

    if (chunk.empty())
    {
        throw std::invalid_argument("Payload chunk is empty.");
    }

    if (isFinalChunk)
    {
        markComplete();
    }

    return chunk;
}

} // namespace pldm::rde
