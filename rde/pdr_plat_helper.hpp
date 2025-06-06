#pragma once

#include <libpldm/platform.h>
#include <libpldm/pldm.h>
#include <libpldm/pldm_types.h>

#include <cstdint>

#pragma pack(push, 1)
struct variable_len_field
{
    uint8_t* ptr;
    uint16_t length;
};

struct oem_info_t
{
    uint8_t* name;
    uint16_t name_length;
};

struct add_resrc_t
{
    uint32_t resrc_id;
    uint16_t length;
    uint8_t* name;
};

// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
struct pldm_redfish_resource_pdr
{
    struct pldm_value_pdr_hdr hdr;
    uint32_t resource_id;
    bitfield8_t resource_flags;
    uint32_t cont_resrc_id;
    uint16_t prop_cont_resrc_length;
    uint8_t* prop_cont_resrc_name;
    uint16_t sub_uri_length;
    uint8_t* sub_uri_name;
    uint16_t add_resrc_id_count;
    struct add_resrc_t** additional_resrc;
    ver32_t major_schema_version;
    uint16_t major_schema_dict_length_bytes;
    uint32_t major_schema_dict_signature;
    struct variable_len_field major_schema;
    uint16_t oem_count;
    struct oem_info_t** oem_list;
};
#pragma pack(pop)
