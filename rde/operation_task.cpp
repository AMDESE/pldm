#include "operation_task.hpp"

#include "device_common.hpp"
#include "manager.hpp"

#include <libpldm/base.h>

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>

#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

PHOSPHOR_LOG2_USING;

namespace pldm::rde
{

namespace
{
bool isTerminalTaskReturnCode(uint16_t returnCode)
{
    return returnCode ==
               static_cast<uint16_t>(OpState::OperationCompleted) ||
           returnCode == static_cast<uint16_t>(OpState::OperationFailed) ||
           returnCode == static_cast<uint16_t>(OpState::Cancelled) ||
           returnCode == static_cast<uint16_t>(OpState::TimedOut);
}

std::optional<uint32_t> operationIdFromTaskPath(const std::string& path)
{
    static constexpr std::string_view prefix =
        "/xyz/openbmc_project/RDE/OperationTask/";
    if (path.compare(0, prefix.size(), prefix) != 0)
    {
        return std::nullopt;
    }

    try
    {
        const unsigned long id = std::stoul(path.substr(prefix.size()));
        return static_cast<uint32_t>(id);
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

void scheduleTaskCleanup(Manager* manager, const std::string& path,
                         uint16_t returnCode)
{
    if (manager == nullptr || !isTerminalTaskReturnCode(returnCode))
    {
        return;
    }

    if (const auto operationId = operationIdFromTaskPath(path))
    {
        manager->scheduleUnregisterOperationTask(*operationId);
    }
}
} // namespace

int emitTaskUpdatedSignal(sdbusplus::bus_t& bus, const std::string& path,
                          const std::string& payload, uint16_t returnCode,
                          Manager* manager)
{
    int rc = PLDM_SUCCESS;

    try
    {
        auto msg = bus.new_signal(path.c_str(),
                                  "xyz.openbmc_project.RDE.OperationTask",
                                  "TaskUpdated");

        std::map<std::string, std::variant<std::string, uint16_t>> changed;
        changed.emplace("payload", payload);
        changed.emplace("return_code", returnCode);
        changed.emplace("CompletionCode", returnCode);

        msg.append(changed);
        msg.signal_send();
        info(
            "RDE: TaskUpdated signal emitted for path={PATH}, return_code={RC}",
            "PATH", path, "RC", returnCode);
    }
    catch (const std::exception& e)
    {
        error(
            "RDE: Failed to emit TaskUpdated signal for path={PATH}, error={ERR}, return_code={RC}",
            "PATH", path, "ERR", e.what(), "RC", returnCode);

        rc = PLDM_ERROR;
    }

    scheduleTaskCleanup(manager, path, returnCode);
    return rc;
}

} // namespace pldm::rde
