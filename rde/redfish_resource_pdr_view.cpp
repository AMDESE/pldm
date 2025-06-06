#include "redfish_resource_pdr_view.hpp"

#include <format>
#include <ranges>

namespace pldm::rde
{

RedfishResourcePDRView::RedfishResourcePDRView(const uint8_t* buffer,
                                               size_t length) :
    cursor(buffer), end(buffer + length)
{}
bool RedfishResourcePDRView::parse(ResourceInfoView& outView)
{
    if (!readUint32(outView.resourceID))
        return false;

    skip(RESOURCE_FLAGS_SIZE);
    skip(CONTAINER_RESOURCE_ID_SIZE);

    if (!readString(outView.propContainerName) || !readString(outView.subURI))
        return false;

    skip(ADD_RESR_ID_COUNT_SIZE);

    uint8_t major, minor, update, alpha;
    if (!readByte(major) || !readByte(minor) || !readByte(update) ||
        !readByte(alpha))
        return false;

    skip(SCHEMA_DICT_LENGTH_SIZE);
    skip(SCHEMA_DICT_SIGNATURE_SIZE);

    if (!readString(outView.schemaName))
        return false;

    uint16_t oemCount;
    if (!readUint16(oemCount))
        return false;

    for (auto _ : std::views::iota(0u, static_cast<unsigned>(oemCount)))
    {
        std::string oem;
        if (!readString(oem))
            return false;
        outView.oemNames.push_back(std::move(oem));
    }

    outView.schemaVersion =
        std::format("{}.{}.{}.{}", major, minor, update, alpha);
    return true;
}

bool RedfishResourcePDRView::readByte(uint8_t& val)
{
    if (cursor + BYTE_SIZE > end)
        return false;
    val = *cursor++;
    return true;
}

bool RedfishResourcePDRView::readUint16(uint16_t& val)
{
    if (cursor + UINT16_SIZE > end)
        return false;
    val = cursor[0] | (cursor[1] << SHIFT_BYTE_1);
    cursor += UINT16_SIZE;
    return true;
}

bool RedfishResourcePDRView::readUint32(uint32_t& val)
{
    if (cursor + UINT32_SIZE > end)
        return false;
    val = cursor[0] | (cursor[1] << SHIFT_BYTE_1) |
          (cursor[2] << SHIFT_BYTE_2) | (cursor[3] << SHIFT_BYTE_3);
    cursor += UINT32_SIZE;
    return true;
}

bool RedfishResourcePDRView::readString(std::string& out)
{
    uint16_t len;
    if (!readUint16(len))
        return false;
    if (cursor + len > end)
        return false;
    out.assign(reinterpret_cast<const char*>(cursor), len);
    cursor += len;
    return true;
}

void RedfishResourcePDRView::skip(size_t bytes)
{
    if (cursor + bytes <= end)
        cursor += bytes;
    else
        cursor = end;
}

} // namespace pldm::rde
