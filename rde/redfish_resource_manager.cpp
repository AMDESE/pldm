#include "redfish_resource_manager.hpp"

#include "pdr_plat_helper.hpp"
#include "redfish_resource_pdr_view.hpp"

#include <libpldm/platform.h>
#include <libpldm/pldm.h>
#include <libpldm/pldm_types.h>

#include <phosphor-logging/lg2.hpp>

PHOSPHOR_LOG2_USING;

#include <cstring>
#include <fstream>
#include <span>

namespace pldm::rde
{

RedfishResourceManager::RedfishResourceManager(pldm_pdr* pdrRepo,
                                               uint32_t deviceID) :
    pdrRepo(pdrRepo), deviceID(deviceID)
{}

std::vector<ResourceInfoView> RedfishResourceManager::extractResourcePDRs()
{
    uint8_t* outData = nullptr;
    uint32_t size{};
    const pldm_pdr_record* record{};
    std::vector<ResourceInfoView> resourceViews;

    try
    {
        do
        {
            record = pldm_pdr_find_record_by_type(
                pdrRepo, PLDM_REDFISH_RESOURCE_PDR, record, &outData, &size);
            if (record && outData && size > sizeof(pldm_value_pdr_hdr))
            {
                // Skip the common PDR header
                uint8_t* payload = outData + sizeof(pldm_pdr_hdr);
                size_t payloadSize = size - sizeof(pldm_pdr_hdr);

                // Use the helper class to parse the payload
                RedfishResourcePDRView view(payload, payloadSize);
                ResourceInfoView info;

                if (view.parse(info))
                {
                    resourceViews.push_back(std::move(info));
                }
                else
                {
                    error("Failed to parse PDR payload");
                }
            }
        } while (record);
    }
    catch (const std::exception& e)
    {
        error("Failed to obtain a record, error - {ERROR}", "ERROR", e);
    }

    return resourceViews;
}

void RedfishResourceManager::populateStoredResources()
{
    std::vector<ResourceInfoView> views = extractResourcePDRs();

    for (const auto& view : views)
    {
        std::vector<std::string> oemNames;
        for (const auto& oem : view.oemNames)
        {
            oemNames.emplace_back(oem);
        }

        storedResources.emplace(
            view.resourceID,
            ResourceInfoView{
                view.resourceID, std::string(view.propContainerName),
                std::string(view.subURI), std::string(view.schemaName),
                view.schemaVersion, std::move(oemNames)});
    }
}

nlohmann::json RedfishResourceManager::buildJSONSchemaMap() const
{
    nlohmann::json jsonMap;
    nlohmann::json resourcesJson;

    for (const auto& [resourceID, resourceInfo] : storedResources)
    {
        nlohmann::json resourceJson;
        resourceJson["ProposedContainingResourceName"] =
            resourceInfo.propContainerName;
        resourceJson["MajorSchemaName"] = resourceInfo.schemaName;
        resourceJson["MajorSchemaVersion"] = resourceInfo.schemaVersion;
        resourceJson["SubURI"] = resourceInfo.subURI;

        // OEM Extensions
        nlohmann::json oemJson = nlohmann::json::array();
        for (const auto& oemName : resourceInfo.oemNames)
        {
            oemJson.push_back(oemName);
        }
        resourceJson["OEMExtensions"] = oemJson;

        resourcesJson[std::to_string(resourceID)] = resourceJson;
    }

    jsonMap["Resources"] = resourcesJson;
    return jsonMap;
}

void RedfishResourceManager::exportResourceSchemaToFile(
    const std::string& filePath)
{
    // Populate internal resource map from PDRs
    populateStoredResources();

    // Build JSON schema map from stored resources
    nlohmann::json jsonMap = buildJSONSchemaMap();

    // Write JSON to file with pretty print
    std::ofstream file(filePath);
    if (file.is_open())
    {
        file << jsonMap.dump(4); // Indent with 4 spaces
        file.close();
    }
    else
    {
        error("Failed to open file: {PATH}", "PATH", filePath.c_str());
    }
}

} // namespace pldm::rde
