#pragma once

#include <libpldm/base.h>
#include <libpldm/rde.h>

#include <phosphor-logging/lg2.hpp>

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

PHOSPHOR_LOG2_USING;

namespace pldm::rde
{
/**
 * @brief Logs human-readable error messages for RDE completion codes.
 *
 * This helper function maps each PLDM RDE completion code to a descriptive
 * error string and logs it using the system logger. It is invoked when
 * an operation returns an error response.
 *
 * @param cc - The 8-bit completion code value returned by the operation
 */
void logCompletionCodeError(uint8_t cc);

/**
 * @brief Logs the hexadecimal representation of a binary payload.
 *
 * Converts each byte of the input payload into a two-character uppercase
 * hexadecimal string, separated by spaces, and logs the full formatted string.
 *
 * Example output: "Payload HEX dump: 01 AF 3B 7C ..."
 *
 * @param payload Vector of bytes representing the binary payload.
 */
void logHexPayload(const std::vector<uint8_t>& payload);
#ifdef OEM_AMD
/**
 * @brief Load processor URI from device metadata file
 *
 * This function reads the RDE device metadata file and looks up the processor
 * URI for a given device UUID. The processor URI is used to identify devices
 * that support caching operations.
 *
 * @param[in] devUUID Device UUID to lookup
 * @return Processor URI string in format "schema/deviceId/", or empty string
 *         if not found
 */
std::string loadProcessorURI(const std::string& devUUID);
#endif

} // namespace pldm::rde
