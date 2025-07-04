#include "discov_session.hpp"

#include "common/instance_id.hpp"
#include "device.hpp"

#include <libpldm/base.h>
#include <libpldm/platform.h>
#include <libpldm/pldm_types.h>
#include <libpldm/rde.h>

#include <phosphor-logging/lg2.hpp>

PHOSPHOR_LOG2_USING;

namespace pldm::rde
{

DiscoverySession::DiscoverySession(Device& device) :
    device_(&device), instanceIdDb_(device.getInstanceIdDb()),
    handler_(device.getHandler()), eid_(device.getEid()), tid_(device.getTid()),
    currentState_(OpState::Idle)
{}

void DiscoverySession::updateState(OpState newState)
{
    currentState_ = newState;
}

OpState DiscoverySession::getState() const
{
    return currentState_;
}

void DiscoverySession::doNegotiateRedfish()
{
    auto instanceId = instanceIdDb_.next(eid_);
    if (!instanceId)
    {
        error("NegotiateRedfishParameters Failed to get instance ID: EID={EID}",
              "EID", eid_);
        return;
    }

    Request request(
        sizeof(pldm_msg_hdr) + PLDM_RDE_NEGOTIATE_REDFISH_PARAMETERS_REQ_BYTES);
    auto requestMsg = new (request.data()) pldm_msg;

    auto mcFeature =
        std::get<FeatureSupport>(device_->getMetadataField("mcFeatureSupport"));
    bitfield16_t mcFeatureBits;
    mcFeatureBits.value = mcFeature.value;

    info("NegotiateRedfishParameters request: EID={EID}, InstanceID={IID},\
       	ConcurrencySupport={CONCURRENCY}, MCFeatureSupport={FEATURE}",
         "EID", eid_, "IID", instanceId, "CONCURRENCY",
         std::get<uint8_t>(device_->getMetadataField("mcConcurrencySupport")),
         "FEATURE",
         std::get<FeatureSupport>(device_->getMetadataField("mcFeatureSupport"))
             .value);
    int rc = encode_negotiate_redfish_parameters_req(
        instanceId,
        std::get<uint8_t>(device_->getMetadataField("mcConcurrencySupport")),
        &mcFeatureBits, requestMsg);

    if (rc != PLDM_SUCCESS)
    {
        error(
            "encode NegotiateRedfishParameters request EID '{EID}', RC '{RC}'",
            "EID", eid_, "RC", rc);

        updateState(OpState::OperationFailed);
        instanceIdDb_.free(eid_, instanceId);
        return;
    }

    rc = handler_->registerRequest(
        eid_, instanceId, PLDM_RDE, PLDM_NEGOTIATE_REDFISH_PARAMETERS,
        std::move(request),
        [this, instanceId](uint8_t eid, const pldm_msg* respMsg, size_t rxLen) {
            instanceIdDb_.free(eid, instanceId);
            this->handleNegotiateRedfishResp(eid, respMsg, rxLen, device_);
        });

    if (rc)
    {
        error(
            "Failed to send request NegotiateRedfishParameters EID '{EID}', RC '{RC}'",
            "EID", eid_, "RC", rc);

        instanceIdDb_.free(eid_, instanceId);

        throw std::runtime_error(
            "Failed to send request NegotiateRedfishParameters");
    }

    info("NegotiateRedfishParameters Request Waiting");
}

void DiscoverySession::handleNegotiateRedfishResp(
    uint8_t eid, const pldm_msg* respMsg, size_t rxLen, Device* device)
{
    if (currentState_ == OpState::TimedOut ||
        currentState_ == OpState::Cancelled)
    {
        info("Late response received. Ignoring callback safely. EID={EID}",
             "EID", eid);
        return;
    }

    info("handleNegotiateRedfishResp Start: EID={EID} rxLen={RXLEN}", "EID",
         eid, "RXLEN", rxLen);

    if (respMsg == nullptr)
    {
        error("Null PLDM response received from Endpoit ID {EID}", "EID", eid);
        updateState(OpState::OperationFailed);
        return;
    }

    // If rxLen is 0, apply fallback — but only use safely capped range
    if (rxLen == 0)
    {
        error("rxLen is 0; Bad response Packet. EID={EID}", "EID", eid);
        updateState(OpState::OperationFailed);
        return;
    }

    uint8_t cc = 0;
    uint8_t devConcurrency = 0;
    bitfield8_t devCaps = {};
    bitfield16_t devFeatures = {};
    uint32_t configSig = 0;
    pldm_rde_varstring providerName = {};

    int rc = decode_negotiate_redfish_parameters_resp(
        respMsg, rxLen, &cc, &devConcurrency, &devCaps, &devFeatures,
        &configSig, &providerName);

    if (rc != PLDM_SUCCESS || cc != PLDM_SUCCESS)
    {
        error(
            "Failed to decode NegotiateRedfishParameters response rc:{RC} cc:{CC}",
            "RC", rc, "CC", cc);
        updateState(OpState::OperationFailed);
        return;
    }

    FeatureSupport features(devFeatures);
    DeviceCapabilities caps{devCaps};

    info(
        "NegotiateRedfishParameters response: EID={EID},Signature={SIG}, Provider={PROV} \
        ConcurrencySupport={CONCURRENCY}, FeatureSupport={FEATURE}, DeviceCapabilities={CAPS}",
        "EID", eid_, "SIG", configSig, "PROV", providerName.string_data,
        "CONCURRENCY", devConcurrency, "FEATURE", features.value, "CAPS",
        caps.value);

    if (device)
    {
        device->setMetadataField("deviceConcurrencySupport", devConcurrency);
        device->setMetadataField("devCapabilities", caps);
        device->setMetadataField("devFeatureSupport", features);
        device->setMetadataField("devConfigSignature", configSig);
    }

    info("NegotiateRedfishParameters Command completed");
    return;
}

} // namespace pldm::rde
