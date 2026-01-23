#include "platform_manager.hpp"

#include "common/utils.hpp"
#include "manager.hpp"
#include "terminus_manager.hpp"

#include <phosphor-logging/lg2.hpp>

#include <filesystem>
#include <ranges>

PHOSPHOR_LOG2_USING;

namespace pldm
{
namespace platform_mc
{

std::vector<uint16_t> convertToUtf16BE(const std::string& text)
{
    std::vector<uint16_t> utf16Data;
    for (char c : text)
    {
        utf16Data.push_back(htobe16(static_cast<uint16_t>(c)));
    }
    return utf16Data;
}

std::optional<std::vector<uint8_t>> decode_entity_auxiliary_names_pdr(
    const nlohmann::json& j)
{
    try
    {
        size_t auxiliaryDataSize = 0;
        const auto& names = j.at("names");
        for (const auto& nameEntry : names)
        {
            std::string lang = nameEntry.at(0).get<std::string>();
            std::string text = nameEntry.at(1).get<std::string>();

            auxiliaryDataSize += (lang.size() + 1);     // Tag + null
            auxiliaryDataSize += (text.size() * 2) + 2; // UTF16-BE + null
        }

        size_t totalSize = 18 + auxiliaryDataSize;

        std::vector<uint8_t> pdrVec(totalSize, 0);
        uint8_t* ptr = pdrVec.data();

        auto hdr = reinterpret_cast<pldm_pdr_hdr*>(ptr);
        hdr->version = 1;
        hdr->type = PLDM_ENTITY_AUXILIARY_NAMES_PDR;
        hdr->length = static_cast<uint16_t>(totalSize - sizeof(pldm_pdr_hdr));

        size_t offset = sizeof(pldm_pdr_hdr);

        pldm_entity container{};
        container.entity_type = j.at("entity_type").get<uint16_t>();
        container.entity_instance_num =
            j.at("entity_instance_number").get<uint16_t>();
        container.entity_container_id = j.at("container_id").get<uint16_t>();
        std::memcpy(ptr + offset, &container, sizeof(container));
        offset += sizeof(container);

        ptr[offset++] = j.at("shared_name_count").get<uint8_t>();
        ptr[offset++] = static_cast<uint8_t>(names.size());

        for (const auto& nameEntry : names)
        {
            std::string lang = nameEntry.at(0).get<std::string>();
            std::memcpy(ptr + offset, lang.c_str(), lang.size());
            ptr[offset + lang.size()] = '\0';
            offset += (lang.size() + 1);

            std::string textStr = nameEntry.at(1).get<std::string>();
            std::vector<uint16_t> utf16Data = convertToUtf16BE(textStr);

            std::memcpy(ptr + offset, utf16Data.data(), utf16Data.size() * 2);
            offset += (utf16Data.size() * 2);

            ptr[offset++] = 0x00;
            ptr[offset++] = 0x00;
        }

        lg2::info("Successfully encoded Auxiliary Name PDR, size: {SIZE}",
                  "SIZE", totalSize);
        return pdrVec;
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to decode Auxiliary Names PDR: {ERR}", "ERR",
                   e.what());
        return std::nullopt;
    }
}

std::optional<std::vector<uint8_t>> decode_compact_numeric_sensor_pdr(
    const pldm_tid_t tid, const nlohmann::json& j)
{
    try
    {
        const std::string name = j.at("sensor_name").get<std::string>();

        size_t pdrSize =
            offsetof(pldm_compact_numeric_sensor_pdr, sensor_name) +
            name.size();

        std::vector<uint8_t> pdrVec(pdrSize, 0);
        auto pdr =
            reinterpret_cast<pldm_compact_numeric_sensor_pdr*>(pdrVec.data());

        pdr->hdr.type = j.at("pdr_type").get<uint16_t>();
        pdr->terminus_handle = tid;
        pdr->sensor_id = j.at("sensor_id").get<uint16_t>();
        pdr->entity_type = j.at("entity_type").get<uint16_t>();
        pdr->entity_instance = j.at("entity_instance_number").get<uint16_t>();
        pdr->container_id = j.at("container_id").get<uint16_t>();

        pdr->sensor_name_length = static_cast<uint8_t>(name.size());
        std::memcpy(pdr->sensor_name, name.data(), name.size());

        pdr->base_unit = j.at("base_unit").get<uint8_t>();
        pdr->unit_modifier = j.at("unit_modifier").get<int8_t>();
        pdr->occurrence_rate = j.at("occurrence_rate").get<uint8_t>();
        pdr->range_field_support.byte =
            j.at("range_field_support").get<uint8_t>();

        pdr->hdr.length = static_cast<uint16_t>(pdrVec.size() - 10);
        uint16_t reportedLength = pdr->hdr.length;

        lg2::info("Successfully decoded Compact PDR: {NAME} of size: {PSIZE}",
                  "NAME", name, "PSIZE", reportedLength);

        return pdrVec;
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to decode Compact Numeric Sensor PDR: {ERR}", "ERR",
                   e.what());
        return std::nullopt;
    }
}

/* Setting default values when getPDRRepositoryInfo fails or does not support */
exec::task<int> PlatformManager::get_pdr_from_json(
    std::shared_ptr<Terminus> terminus)
{
    pldm_tid_t tid = terminus->getTid();

    std::string pdrDir = "/usr/share/pldm/pdr/com.amd.Hardware.Chassis";
    if (!std::filesystem::exists(pdrDir))
    {
        co_return PLDM_ERROR_INVALID_DATA;
    }

    try
    {
        // Only iterate immediate contents of pdrDir
        for (const auto& subDir : std::filesystem::directory_iterator(pdrDir))
        {
            // IGNORE all files in the top pdrDir; only enter subdirectories
            if (!subDir.is_directory())
            {
                continue;
            }

            // Only iterate immediate contents of the subdirectory (e.g.,
            // 'Processor0')
            for (const auto& subFile :
                 std::filesystem::directory_iterator(subDir.path()))
            {
                // IGNORE everything except regular .json files
                if (subFile.is_regular_file() &&
                    subFile.path().extension() == ".json")
                {
                    std::ifstream jsonFile(subFile.path());
                    auto jData =
                        nlohmann::ordered_json::parse(jsonFile, nullptr, false);

                    if (jData.is_discarded())
                    {
                        lg2::error("Parsing JSON failed for {PATH}", "PATH",
                                   subFile.path().string());
                        continue; // Skip bad file, continue to next
                    }

                    for (const auto& [pdrName, pdrArray] : jData.items())
                    {
                        // Strict check: ignore keys that aren't arrays
                        if (!pdrArray.is_array())
                        {
                            continue;
                        }

                        for (const auto& pdrJson : pdrArray)
                        {
                            if (pdrName == "entityAuxiliaryNamePDRs")
                            {
                                if (auto pdrVec =
                                        decode_entity_auxiliary_names_pdr(
                                            pdrJson))
                                {
                                    terminus->pdrs.emplace_back(
                                        std::move(*pdrVec));
                                }
                            }
                            else if (pdrName == "compactNumericSensorPDRs")
                            {
                                if (auto pdrVec =
                                        decode_compact_numeric_sensor_pdr(
                                            tid, pdrJson))
                                {
                                    terminus->pdrs.emplace_back(
                                        std::move(*pdrVec));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        // lg2::error("Filesystem access error: {ERROR}", "ERROR", e.what());
        lg2::error("Filesystem access error: {ERROR}, path: {PATH}", "ERROR",
                   e.what(), "PATH", e.path1());
        co_return PLDM_ERROR;
    }

    co_return PLDM_SUCCESS;
}

} // namespace platform_mc
} // namespace pldm
