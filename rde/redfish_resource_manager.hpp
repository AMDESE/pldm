#pragma once

#include "pdr_plat_helper.hpp"
#include "redfish_resource_pdr_view.hpp"

#include <libpldm/platform.h>
#include <libpldm/pldm.h>
#include <libpldm/pldm_types.h>

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace pldm::rde
{

/**
 * @class RedfishResourceManager
 * @brief Class to manage Redfish resources and actions.
 *
 * This class is responsible for parsing Redfish  PLDM PDRs and generating JSON
 * schema maps for Redfish resources. It is designed to be extensible to support
 * additional PDR types in the future.
 */
class RedfishResourceManager
{
  public:
    /**
     * @brief Constructor for the RedfishResourceManager class.
     * @param pdrRepo Pointer to the PLDM PDR repository.
     * @param deviceID ID of the device.
     */
    RedfishResourceManager(pldm_pdr* pdrRepo, uint32_t deviceID);
    RedfishResourceManager(const RedfishResourceManager&) = default;
    RedfishResourceManager& operator=(const RedfishResourceManager&) = default;
    RedfishResourceManager(RedfishResourceManager&&) = default;
    RedfishResourceManager& operator=(RedfishResourceManager&&) = default;

    /**
     * @brief Destructor for the RedfishResourceManager class.
     */
    virtual ~RedfishResourceManager() = default;

    /**
     * @brief Exports the resource schema to a JSON file.
     *
     * This function populates the internal resource map by extracting
     * supported PDRs and then builds a JSON schema map. The resulting
     * JSON is written to the specified file path.
     *
     * @param filePath Path to the output JSON file.
     */
    void exportResourceSchemaToFile(const std::string& filePath);

    /**
     * @brief Extracts structured resource information from supported PDR types.
     *
     * This function parses the Platform Descriptor Records (PDRs) repository
     * and extracts structured resource metadata into a list of ResourceInfoView
     * objects. It currently supports Redfish Resource PDRs, but is designed to
     * be extended to handle additional PDR types in the future.
     *
     * @return A vector of ResourceInfoView structures representing the parsed
     *         resource metadata from the PDR repository.
     */
    virtual std::vector<ResourceInfoView> extractResourcePDRs();

  private:
    /**
     * @brief Populates the internal resource map with structured resource data.
     *
     * This function serves as a generic mechanism to populate the internal
     * storedResources map with resource information extracted from Platform
     * Descriptor Records (PDRs). It currently supports Redfish Resource PDRs
     * by invoking the extractResourcePDRs() helper function.
     *
     * @note This function is designed to be extensible. In the future, it can
     * be adapted to support additional PDR types or resource categories.
     */
    void populateStoredResources();

    /**
     * @brief Builds a JSON schema map from the extracted resources and actions.
     * @return JSON schema map.
     */
    nlohmann::json buildJSONSchemaMap() const;

    pldm_pdr* pdrRepo;                  // Pointer to the PLDM PDR repository.
    [[maybe_unused]] uint32_t deviceID; // ID of the device.
    std::unordered_map<uint32_t, ResourceInfoView>
        storedResources;                // Map of stored resources.
};

} // namespace pldm::rde
