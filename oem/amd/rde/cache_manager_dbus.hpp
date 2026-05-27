#pragma once

#include "rde_cache_manager.hpp"
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/RDE/CacheManager/server.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>
#include <string>
#include <variant>
#include <vector>

PHOSPHOR_LOG2_USING;

namespace pldm::rde
{

using CacheManagerIface = sdbusplus::server::object_t<
    sdbusplus::xyz::openbmc_project::RDE::server::CacheManager>;
using APCBValue = std::variant<int64_t, std::string>;
using APCBEntry = std::map<std::string, APCBValue>;
using APCBDataTableType = std::map<std::string, APCBEntry>;

class CacheManagerObject : public CacheManagerIface
{
  public:
    CacheManagerObject(sdbusplus::bus_t& bus, const char* path) :
        CacheManagerIface(bus, path)
    {
        loadAPCBDataTableFromDisk();
    }

    /**
     * @brief Implementation for GetCurrentCache
     * Retrieve all cached operations for a specific RDE device.
     *
     * @param[in] uuid - The UUID of the device to query.
     * @return Operations - List of cached operations as JSON strings.
     */
    std::vector<std::string> getCurrentCache(std::string uuid) override;

    void updateAPCBDataTable(const std::string& uuid,
                             const nlohmann::json& parsedPayload)
    {
        auto table = this->apcbDataTable();
        table[uuid] = jsonToAPCBEntry(parsedPayload);

        this->apcbDataTable(table);
        persistAPCBDataTableToDisk(table);
    }

    static nlohmann::json apcbEntryToJson(const APCBEntry& entry)
    {
        nlohmann::json payload = nlohmann::json::object();
        for (const auto& [key, value] : entry)
        {
            std::visit(
                [&](const auto& v) { payload[key] = v; }, value);
        }
        return payload;
    }

    void loadAPCBDataTableFromDisk()
    {
        std::ifstream inFile(apcbPersistFile);
        if (!inFile.is_open())
        {
            info("RDE: APCB persistence file does not exist yet: {FILE}",
                 "FILE", apcbPersistFile);
            this->apcbDataTable(APCBDataTableType{});
            return;
        }

        try
        {
            nlohmann::json j = nlohmann::json::object();
            inFile >> j;

            APCBDataTableType table = jsonToAPCBDataTable(j);
            this->apcbDataTable(table);

            info("RDE: Restored APCBDataTable from {FILE}",
                 "FILE", apcbPersistFile);
        }
        catch (const std::exception& e)
        {
            error("RDE: Failed to restore APCBDataTable from {FILE}: {ERR}",
                  "FILE", apcbPersistFile, "ERR", e.what());
            this->apcbDataTable(APCBDataTableType{});
        }
    }

    /**
     * @brief Implementation for CreateCache
     * Manually create a cache entry for a specific RDE device.
     * Parameter order matches StartRedfishOperation (excluding OperationID and EID).
     *
     * @param[in] operationType - Operation type
     * @param[in] targetURI - Target URI
     * @param[in] deviceUUID - Device UUID
     * @param[in] payload - Operation payload (JSON string)
     * @param[in] payloadFormat - Payload format (default: Inline)
     * @param[in] encodingFormat - Encoding format (default: JSON)
     * @param[in] sessionId - Session ID (optional)
     * @return bool - true if successfully cached, false otherwise
     */
    bool createCache(
        sdbusplus::common::xyz::openbmc_project::rde::Common::OperationType
            operationType,
        std::string targetURI, std::string deviceUUID,
        std::string payload,
        sdbusplus::xyz::openbmc_project::RDE::server::Manager::PayloadFormatType
            payloadFormat,
        sdbusplus::xyz::openbmc_project::RDE::server::Manager::EncodingFormatType
            encodingFormat,
        std::string sessionId) override;

  private:
    static constexpr const char* apcbPersistFile =
        "/var/lib/pldm/apcb_data_table.json";

    bool persistAPCBDataTableToDisk(const APCBDataTableType& table)
    {
        try
        {
            std::filesystem::create_directories("/var/lib/pldm");

            std::ofstream outFile(apcbPersistFile);
            if (!outFile.is_open())
            {
                error("RDE: Failed to open APCB persistence file {FILE}",
                      "FILE", apcbPersistFile);
                return false;
            }

            nlohmann::json j = apcbDataTableToJson(table);
            outFile << j.dump(4) << std::endl;

            info("RDE: Persisted APCBDataTable to {FILE}",
                 "FILE", apcbPersistFile);
            return true;
        }
        catch (const std::exception& e)
        {
            error("RDE: Failed to persist APCBDataTable: {ERR}",
                  "ERR", e.what());
            return false;
        }
    }

    static APCBEntry jsonToAPCBEntry(const nlohmann::json& j)
    {
        APCBEntry entry;

        if (!j.is_object())
        {
            throw std::runtime_error("APCB JSON payload is not an object");
        }

        for (const auto& [key, value] : j.items())
        {
            if (value.is_number_integer())
            {
                entry[key] = value.get<int64_t>();
            }
            else if (value.is_string())
            {
                entry[key] = value.get<std::string>();
            }
            else if (value.is_number_unsigned())
            {
                entry[key] = static_cast<int64_t>(value.get<uint64_t>());
            }
            else if (value.is_boolean())
            {
                entry[key] = value.get<bool>() ? "true" : "false";
            }
            else if (value.is_number_float())
            {
                entry[key] = std::to_string(value.get<double>());
            }
            else
            {
                entry[key] = value.dump();
            }
        }

        return entry;
    }

    static APCBDataTableType jsonToAPCBDataTable(const nlohmann::json& j)
    {
        APCBDataTableType table;

        if (!j.is_object())
        {
            throw std::runtime_error("Persisted APCB table JSON is not an object");
        }

        for (const auto& [uuid, value] : j.items())
        {
            table[uuid] = jsonToAPCBEntry(value);
        }

        return table;
    }

    static nlohmann::json apcbDataTableToJson(const APCBDataTableType& table)
    {
        nlohmann::json j = nlohmann::json::object();

        for (const auto& [uuid, entry] : table)
        {
            nlohmann::json je = nlohmann::json::object();

            for (const auto& [key, value] : entry)
            {
                std::visit(
                    [&](const auto& v)
                    {
                        je[key] = v;
                    },
                    value);
            }

            j[uuid] = je;
        }

        return j;
    }
};

} // namespace pldm::rde

