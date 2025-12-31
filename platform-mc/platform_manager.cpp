#include "platform_manager.hpp"

#include "common/utils.hpp"
#include "manager.hpp"
#include "terminus_manager.hpp"

#include <phosphor-logging/lg2.hpp>

#include <ranges>
#include <arpa/inet.h>

PHOSPHOR_LOG2_USING;

namespace pldm
{
namespace platform_mc
{

#define EXTRACT_FIELD_FROM_JSON(jRecord, jField, structMember )\
	if (jRecord.contains(jField)) \
		structMember = jRecord[jField]; \
	else \
		std::cout << jRecord << "======= NOT FOUND.\n"; \
	std::cout << jField << ": " << std::hex << (unsigned) structMember << "(" << sizeof(structMember) << ")\n";

void pdrFromJson(auto& record,  pldm_pdr_hdr& hdr)
{
    EXTRACT_FIELD_FROM_JSON(record, "recordHandle", hdr.record_handle);
    std::cout << "pdrFromJson(): hdr.record_handle = " << hdr.record_handle << "\n";
    EXTRACT_FIELD_FROM_JSON(record, "PDRHeaderVersion", hdr.version);
    std::cout << "pdrFromJson(): hdr.version = " << hdr.version << "\n";
    EXTRACT_FIELD_FROM_JSON(record, "PDRType", hdr.type);
    std::cout << "pdrFromJson(): hdr.type = " << hdr.type << "\n";
    EXTRACT_FIELD_FROM_JSON(record, "recordChangeNumber", hdr.record_change_num);
    EXTRACT_FIELD_FROM_JSON(record, "dataLength", hdr.length);
    std::cout << "pdrFromJson(): hdr.dataLength = " << hdr.length << "\n";
}
void pdrFromJson(auto& record,  pldm_value_pdr_hdr& hdr)
{
    EXTRACT_FIELD_FROM_JSON(record, "recordHandle", hdr.record_handle);
    EXTRACT_FIELD_FROM_JSON(record, "PDRHeaderVersion", hdr.version);
    EXTRACT_FIELD_FROM_JSON(record, "PDRType", hdr.type);
    EXTRACT_FIELD_FROM_JSON(record, "recordChangeNumber", hdr.record_change_num);
    EXTRACT_FIELD_FROM_JSON(record, "dataLength", hdr.length);
}

void pdrFromJson(auto& record, pldm_terminus_locator_pdr& pdr)
{
    pdrFromJson(record, pdr.hdr);
    EXTRACT_FIELD_FROM_JSON(record,
                            "PLDMTerminusHandle", pdr.terminus_handle);
    EXTRACT_FIELD_FROM_JSON(record, "validity", pdr.validity);
    EXTRACT_FIELD_FROM_JSON(record, "TID", pdr.tid);
    EXTRACT_FIELD_FROM_JSON(record, "containerID", pdr.container_id);
    EXTRACT_FIELD_FROM_JSON(record,
                        "terminusLocatorType", pdr.terminus_locator_type);
    EXTRACT_FIELD_FROM_JSON(record,
            "terminusLocatorValueSize", pdr.terminus_locator_value_size);
    EXTRACT_FIELD_FROM_JSON(record, "EID", pdr.terminus_locator_value[0]);
}

void pdrPossibleStatesFromJson(auto& record, uint32_t i, std::vector<uint8_t>& pdrBytes)
{
    auto stateSetId = "stateSetID["+std::to_string(i)+"]";
    if (record.contains(stateSetId))
    {
        uint16_t state_set_id = record[stateSetId];
        pdrBytes.insert(pdrBytes.end(), (uint8_t)(state_set_id & 0xFF));
        pdrBytes.insert(pdrBytes.end(), (uint8_t)((state_set_id & 0xFF00) >> 8));
    }

    auto stateSize = "possibleStatesSize["+std::to_string(i)+"]";
    if (record.contains(stateSize))
    {
        uint8_t possible_states_size = (uint8_t)record[stateSize];
        pdrBytes.insert(pdrBytes.end(), &possible_states_size,
                      &possible_states_size + sizeof(possible_states_size));
    }

    auto states = "possibleStates["+std::to_string(i)+"]";
    if (record.contains(states))
    {
        for(uint8_t state: record[states])
            pdrBytes.insert(pdrBytes.end(), state);
    }
}

void pdrFromJson(auto& record, pldm_state_sensor_pdr& pdr,
                 std::vector<uint8_t>& pdrBytes)
{
    pdrFromJson(record, pdr.hdr);
    EXTRACT_FIELD_FROM_JSON(record,
                            "PLDMTerminusHandle", pdr.terminus_handle);
    EXTRACT_FIELD_FROM_JSON(record, "sensorID", pdr.sensor_id);
    EXTRACT_FIELD_FROM_JSON(record, "entityType", pdr.entity_type);
    EXTRACT_FIELD_FROM_JSON(record, "entityInstanceNumber",
                            pdr.entity_instance);
    EXTRACT_FIELD_FROM_JSON(record, "containerID", pdr.container_id);
    EXTRACT_FIELD_FROM_JSON(record, "sensorInit", pdr.sensor_init);
    EXTRACT_FIELD_FROM_JSON(record, "sensorAuxiliaryNamesPDR",
                            pdr.sensor_auxiliary_names_pdr);
    EXTRACT_FIELD_FROM_JSON(record, "compositeSensorCount",
                            pdr.composite_sensor_count);
    pdrBytes.resize(sizeof(pldm_state_sensor_pdr)-1);
    std::memcpy(pdrBytes.data(), &pdr, sizeof(pldm_state_sensor_pdr)-1);

    // retrieve possible states
    for(uint32_t i=0; i<pdr.composite_sensor_count; i++)
    {
        pdrPossibleStatesFromJson(record, i, pdrBytes);
    }
}

void sensorDataSizeFromJson(uint8_t sDataSize,
                            uint32_t field,
                            std::vector<uint8_t>& pdrBytes)
{
    switch(sDataSize)
    {
        case 5:
        case 4:
            pdrBytes.insert(pdrBytes.end(),
                            (uint8_t)(field & 0xFF));
            pdrBytes.insert(pdrBytes.end(), (uint8_t)((field & 0xFF00) >> 8));
            pdrBytes.insert(pdrBytes.end(),
                            (uint8_t)((field & 0xFF0000) >> 16));
            pdrBytes.insert(pdrBytes.end(),
                            (uint8_t)((field & 0xFF000000) >> 24));
            break;
        case 3:
        case 2:
            pdrBytes.insert(pdrBytes.end(), (uint8_t)(field & 0xFF));
            pdrBytes.insert(pdrBytes.end(), (uint8_t)((field & 0xFF00) >> 8));
            break;

        case 1:
        case 0:
            pdrBytes.insert(pdrBytes.end(), (uint8_t)(field & 0xFF));
            break;
        default:
            break;
    }
}

void rangeFieldFromJson(uint8_t rangeFieldFormat,
                        uint32_t field,
                        std::vector<uint8_t>& pdrBytes)
{
    switch(rangeFieldFormat)
    {
        case PLDM_RANGE_FIELD_FORMAT_UINT32:
        case PLDM_RANGE_FIELD_FORMAT_SINT32:
        case PLDM_RANGE_FIELD_FORMAT_REAL32:
            pdrBytes.insert(pdrBytes.end(), (uint8_t)(field & 0xFF));
            pdrBytes.insert(pdrBytes.end(), (uint8_t)((field & 0xFF00) >> 8));
            pdrBytes.insert(pdrBytes.end(),
                                (uint8_t)((field & 0xFF0000) >> 16));
            pdrBytes.insert(pdrBytes.end(),
                                (uint8_t)((field & 0xFF000000) >> 24));
            break;
        case PLDM_RANGE_FIELD_FORMAT_UINT16:
        case PLDM_RANGE_FIELD_FORMAT_SINT16:
            pdrBytes.insert(pdrBytes.end(), (uint8_t)(field & 0xFF));
            pdrBytes.insert(pdrBytes.end(), (uint8_t)((field & 0xFF00) >> 8));
            break;

        case PLDM_RANGE_FIELD_FORMAT_UINT8:
        case PLDM_RANGE_FIELD_FORMAT_SINT8:
            pdrBytes.insert(pdrBytes.end(), (uint8_t)(field & 0xFF));
            break;
        default:
            break;
    }
}

void numericPdrFromJson(auto& record, std::vector<uint8_t>& pdrBytes)
{
    pldm_numeric_sensor_value_pdr pdr = {};

    pdrBytes.clear();

    pdrFromJson(record, pdr.hdr);
    pdrBytes.insert(pdrBytes.begin(), reinterpret_cast<uint8_t*>(&pdr.hdr.record_handle),
                reinterpret_cast<uint8_t*>(&pdr.hdr.record_handle) +
                sizeof(pdr.hdr.record_handle));
    pdrBytes.insert(pdrBytes.end(), (uint8_t)pdr.hdr.version);
    pdrBytes.insert(pdrBytes.end(), (uint8_t)pdr.hdr.type);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.hdr.record_change_num),
                reinterpret_cast<uint8_t*>(&pdr.hdr.record_change_num) +
                sizeof(pdr.hdr.record_change_num));
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.hdr.length),
                reinterpret_cast<uint8_t*>(&pdr.hdr.length) +
                sizeof(pdr.hdr.length));
    EXTRACT_FIELD_FROM_JSON(record,
                            "PLDMTerminusHandle", pdr.terminus_handle);
    EXTRACT_FIELD_FROM_JSON(record, "sensorID", pdr.sensor_id);
    EXTRACT_FIELD_FROM_JSON(record, "entityType", pdr.entity_type);
    EXTRACT_FIELD_FROM_JSON(record, "entityInstanceNumber",
                            pdr.entity_instance);
    EXTRACT_FIELD_FROM_JSON(record, "containerID", pdr.container_id);
    EXTRACT_FIELD_FROM_JSON(record, "sensorInit", pdr.sensor_init);
    EXTRACT_FIELD_FROM_JSON(record, "sensorAuxiliaryNamesPDR",
                            pdr.sensor_auxiliary_names_pdr);
    EXTRACT_FIELD_FROM_JSON(record, "baseUnit", pdr.base_unit);
    EXTRACT_FIELD_FROM_JSON(record, "unitModifier", pdr.unit_modifier);
    EXTRACT_FIELD_FROM_JSON(record, "rateUnit", pdr.rate_unit);
    EXTRACT_FIELD_FROM_JSON(record, "baseOEMUnitHandle",
                            pdr.base_oem_unit_handle);
    EXTRACT_FIELD_FROM_JSON(record, "auxUnit", pdr.aux_unit);
    EXTRACT_FIELD_FROM_JSON(record, "auxUnitModifier", pdr.aux_unit_modifier);
    EXTRACT_FIELD_FROM_JSON(record, "auxrateUnit", pdr.aux_rate_unit);
    EXTRACT_FIELD_FROM_JSON(record, "rel", pdr.rel);
    EXTRACT_FIELD_FROM_JSON(record, "auxOEMUnitHandle",
                            pdr.aux_oem_unit_handle);
    EXTRACT_FIELD_FROM_JSON(record, "isLinear", pdr.is_linear);
    pdrBytes.insert(pdrBytes.end(),
                reinterpret_cast<uint8_t*>(&pdr.terminus_handle),
                reinterpret_cast<uint8_t*>(&pdr.terminus_handle) +
                offsetof(pldm_numeric_sensor_value_pdr, sensor_data_size) -
                offsetof(pldm_numeric_sensor_value_pdr, terminus_handle));

    EXTRACT_FIELD_FROM_JSON(record, "sensorDataSize", pdr.sensor_data_size);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.sensor_data_size),
                reinterpret_cast<uint8_t*>(&pdr.sensor_data_size) +
                sizeof(pdr.sensor_data_size));

    EXTRACT_FIELD_FROM_JSON(record, "resolution", pdr.resolution);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.resolution),
                reinterpret_cast<uint8_t*>(&pdr.resolution) +
                sizeof(pdr.resolution));
    EXTRACT_FIELD_FROM_JSON(record, "offset", pdr.offset);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.offset),
                reinterpret_cast<uint8_t*>(&pdr.offset) +
                sizeof(pdr.offset));
    EXTRACT_FIELD_FROM_JSON(record, "accuracy", pdr.accuracy);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.accuracy),
                reinterpret_cast<uint8_t*>(&pdr.accuracy) +
                sizeof(pdr.accuracy));
    EXTRACT_FIELD_FROM_JSON(record, "plusTolerance", pdr.plus_tolerance);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.plus_tolerance),
                reinterpret_cast<uint8_t*>(&pdr.plus_tolerance) +
                sizeof(pdr.plus_tolerance));
    EXTRACT_FIELD_FROM_JSON(record, "minusTolerance", pdr.minus_tolerance);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.minus_tolerance),
                reinterpret_cast<uint8_t*>(&pdr.minus_tolerance) +
                sizeof(pdr.minus_tolerance));

    // hysteresis - depends on sensorDataSize
    uint32_t hysteresis;
    EXTRACT_FIELD_FROM_JSON(record, "hysteresis", hysteresis);
    sensorDataSizeFromJson(pdr.sensor_data_size, hysteresis, pdrBytes);

    uint8_t supported_thresholds;
    EXTRACT_FIELD_FROM_JSON(record, "supportedThresholds",
                            supported_thresholds);
    pdrBytes.insert(pdrBytes.end(), supported_thresholds);

    uint8_t threshold_and_hysteresis_volatility;
    EXTRACT_FIELD_FROM_JSON(record, "thresholAndHysteresisVolatility",
                            threshold_and_hysteresis_volatility);
    pdrBytes.insert(pdrBytes.end(), threshold_and_hysteresis_volatility);

    real32_t interval;
    EXTRACT_FIELD_FROM_JSON(record, "stateTransitionInterval", interval);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&interval),
                reinterpret_cast<uint8_t*>(&interval) + sizeof(interval));

    EXTRACT_FIELD_FROM_JSON(record, "updateInterval", interval);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&interval),
                reinterpret_cast<uint8_t*>(&interval) + sizeof(interval));

    // maxReadable - depends on sensorDataSize.
    int32_t maxReadable;
    EXTRACT_FIELD_FROM_JSON(record, "maxReadable", maxReadable);
    sensorDataSizeFromJson(pdr.sensor_data_size, maxReadable, pdrBytes);

    // minReadable - depends on sensorDataSize.
    int32_t minReadable;
    EXTRACT_FIELD_FROM_JSON(record, "minReadable", minReadable);
    sensorDataSizeFromJson(pdr.sensor_data_size, minReadable, pdrBytes);

    EXTRACT_FIELD_FROM_JSON(record, "rangeFieldFormat", pdr.range_field_format);
    pdrBytes.insert(pdrBytes.end(), (uint8_t)pdr.range_field_format);

    uint8_t range_field_support;
    EXTRACT_FIELD_FROM_JSON(record, "rangeFieldSupport", range_field_support);
    pdrBytes.insert(pdrBytes.end(), range_field_support);

    // nominalValue -  size of this field is given by the rangeFieldFormat field in this PDR.
    uint32_t field;
    EXTRACT_FIELD_FROM_JSON(record, "nominalValue", field);
    rangeFieldFromJson(pdr.range_field_format, field, pdrBytes);

    EXTRACT_FIELD_FROM_JSON(record, "normalMax", field);
    rangeFieldFromJson(pdr.range_field_format, field, pdrBytes);

    EXTRACT_FIELD_FROM_JSON(record, "normalMin", field);
    rangeFieldFromJson(pdr.range_field_format, field, pdrBytes);

    EXTRACT_FIELD_FROM_JSON(record, "warningHigh", field);
    rangeFieldFromJson(pdr.range_field_format, field, pdrBytes);

    EXTRACT_FIELD_FROM_JSON(record, "warningLow", field);
    rangeFieldFromJson(pdr.range_field_format, field, pdrBytes);

    EXTRACT_FIELD_FROM_JSON(record, "criticalHigh", field);
    rangeFieldFromJson(pdr.range_field_format, field, pdrBytes);

    EXTRACT_FIELD_FROM_JSON(record, "criticalLow", field);
    rangeFieldFromJson(pdr.range_field_format, field, pdrBytes);

    EXTRACT_FIELD_FROM_JSON(record, "fatalHigh", field);
    rangeFieldFromJson(pdr.range_field_format, field, pdrBytes);

    EXTRACT_FIELD_FROM_JSON(record, "fatalLow", field);
    rangeFieldFromJson(pdr.range_field_format, field, pdrBytes);
}

void compactNumericPdrFromJson(auto& record, std::vector<uint8_t>& pdrBytes)
{
    pldm_compact_numeric_sensor_pdr pdr = {};
    std::string sensorName;
    uint32_t field;

    pdrBytes.clear();

    pdrFromJson(record, pdr.hdr);
    pdrBytes.insert(pdrBytes.begin(), reinterpret_cast<uint8_t*>(&pdr.hdr.record_handle),
                reinterpret_cast<uint8_t*>(&pdr.hdr.record_handle) +
                sizeof(pdr.hdr.record_handle));
    pdrBytes.insert(pdrBytes.end(), (uint8_t)pdr.hdr.version);
    pdrBytes.insert(pdrBytes.end(), (uint8_t)pdr.hdr.type);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.hdr.record_change_num),
                reinterpret_cast<uint8_t*>(&pdr.hdr.record_change_num) +
                sizeof(pdr.hdr.record_change_num));
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.hdr.length),
                reinterpret_cast<uint8_t*>(&pdr.hdr.length) +
                sizeof(pdr.hdr.length));

    EXTRACT_FIELD_FROM_JSON(record, "PLDMTerminusHandle", pdr.terminus_handle);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.terminus_handle),
	reinterpret_cast<uint8_t*>(&pdr.terminus_handle) + sizeof(pdr.terminus_handle));

    EXTRACT_FIELD_FROM_JSON(record, "sensorID", pdr.sensor_id);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.sensor_id),
	reinterpret_cast<uint8_t*>(&pdr.sensor_id) + sizeof(pdr.sensor_id));

    EXTRACT_FIELD_FROM_JSON(record, "entityType", pdr.entity_type);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.entity_type),
	reinterpret_cast<uint8_t*>(&pdr.entity_type) + sizeof(pdr.entity_type));

    EXTRACT_FIELD_FROM_JSON(record, "entityInstanceNumber", pdr.entity_instance);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.entity_instance),
	reinterpret_cast<uint8_t*>(&pdr.entity_instance) + sizeof(pdr.entity_instance));

    EXTRACT_FIELD_FROM_JSON(record, "containerID", pdr.container_id);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.container_id),
	reinterpret_cast<uint8_t*>(&pdr.container_id) + sizeof(pdr.container_id));

    EXTRACT_FIELD_FROM_JSON(record, "sensorNameStringByteLength", pdr.sensor_name_length);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.sensor_name_length),
	 reinterpret_cast<uint8_t*>(&pdr.sensor_name_length) + sizeof(pdr.sensor_name_length));

    EXTRACT_FIELD_FROM_JSON(record, "baseUnit", pdr.base_unit);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.base_unit),
	reinterpret_cast<uint8_t*>(&pdr.base_unit) + sizeof(pdr.base_unit));

    EXTRACT_FIELD_FROM_JSON(record, "unitModifier", pdr.unit_modifier);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.unit_modifier),
	reinterpret_cast<uint8_t*>(&pdr.unit_modifier) + sizeof(pdr.unit_modifier));

    EXTRACT_FIELD_FROM_JSON(record, "occuranceRate", pdr.occurrence_rate);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.occurrence_rate),
	reinterpret_cast<uint8_t*>(&pdr.occurrence_rate) + sizeof(pdr.occurrence_rate));

    EXTRACT_FIELD_FROM_JSON(record, "rangeFieldSupport", pdr.range_field_support.byte);
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.range_field_support.byte),
	reinterpret_cast<uint8_t*>(&pdr.range_field_support.byte) + sizeof(pdr.range_field_support.byte));

    EXTRACT_FIELD_FROM_JSON(record, "warningHigh", field);
    rangeFieldFromJson(PLDM_RANGE_FIELD_FORMAT_SINT32, field, pdrBytes);

    EXTRACT_FIELD_FROM_JSON(record, "warningLow", field);
    rangeFieldFromJson(PLDM_RANGE_FIELD_FORMAT_SINT32, field, pdrBytes);

    EXTRACT_FIELD_FROM_JSON(record, "criticalHigh", field);
    rangeFieldFromJson(PLDM_RANGE_FIELD_FORMAT_SINT32, field, pdrBytes);

    EXTRACT_FIELD_FROM_JSON(record, "criticalLow", field);
    rangeFieldFromJson(PLDM_RANGE_FIELD_FORMAT_SINT32, field, pdrBytes);

    EXTRACT_FIELD_FROM_JSON(record, "fatalHigh", field);
    rangeFieldFromJson(PLDM_RANGE_FIELD_FORMAT_SINT32, field, pdrBytes);

    EXTRACT_FIELD_FROM_JSON(record, "fatalLow", field);
    rangeFieldFromJson(PLDM_RANGE_FIELD_FORMAT_SINT32, field, pdrBytes);

    sensorName = record.at("sensorNameString").template get<std::string>();
    pdrBytes.insert(pdrBytes.end(), reinterpret_cast<const uint8_t*>(sensorName.data()),
	reinterpret_cast<const uint8_t*>(sensorName.data()) + sensorName.size());
}

void entityAuxNamesPdrFromJson(auto& record, std::vector<uint8_t>& pdrBytes)
{
	pldm_entity_auxiliary_names_pdr pdr = {};

	pdrBytes.clear();

	pdrFromJson(record, pdr.hdr);
	pdrBytes.insert(pdrBytes.begin(), reinterpret_cast<uint8_t*>(&pdr.hdr.record_handle),
		reinterpret_cast<uint8_t*>(&pdr.hdr.record_handle) + sizeof(pdr.hdr.record_handle));
	pdrBytes.insert(pdrBytes.end(), (uint8_t)pdr.hdr.version);
	pdrBytes.insert(pdrBytes.end(), (uint8_t)pdr.hdr.type);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.hdr.record_change_num),
		reinterpret_cast<uint8_t*>(&pdr.hdr.record_change_num) + sizeof(pdr.hdr.record_change_num));
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.hdr.length),
		reinterpret_cast<uint8_t*>(&pdr.hdr.length) + sizeof(pdr.hdr.length));

	EXTRACT_FIELD_FROM_JSON(record, "EntityType", pdr.container.entity_type);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.container.entity_type),
		reinterpret_cast<uint8_t*>(&pdr.container.entity_type) + sizeof(pdr.container.entity_type));

	EXTRACT_FIELD_FROM_JSON(record, "EntityInstanceNumber", pdr.container.entity_instance_num);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.container.entity_instance_num),
		reinterpret_cast<uint8_t*>(&pdr.container.entity_instance_num) + sizeof(pdr.container.entity_instance_num));

	EXTRACT_FIELD_FROM_JSON(record, "EntityContainerID", pdr.container.entity_container_id);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.container.entity_container_id),
		reinterpret_cast<uint8_t*>(&pdr.container.entity_container_id) + sizeof(pdr.container.entity_container_id));


	EXTRACT_FIELD_FROM_JSON(record, "SharedNameCount", pdr.shared_name_count);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.shared_name_count),
		reinterpret_cast<uint8_t*>(&pdr.shared_name_count) + sizeof(pdr.shared_name_count));

	EXTRACT_FIELD_FROM_JSON(record, "NameStringCount", pdr.name_string_count);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.name_string_count),
		reinterpret_cast<uint8_t*>(&pdr.name_string_count) + sizeof(pdr.name_string_count));

	std::string langTag = record["Names"][0][0].template get<std::string>();
	pdrBytes.insert(pdrBytes.end(), langTag.begin(), langTag.end());
        pdrBytes.push_back('\0');

	std::string nameTag = record["Names"][0][1].template get<std::string>();
        for (char c : nameTag) {
            uint16_t beChar = htons(static_cast<uint16_t>(c));
            uint8_t bytes[2];
            std::memcpy(bytes, &beChar, 2);
            pdrBytes.push_back(bytes[0]);
            pdrBytes.push_back(bytes[1]);
        }
        pdrBytes.push_back(0x00);
        pdrBytes.push_back(0x00);
}

void numericEffecterValuePdrFromJson(auto& record, std::vector<uint8_t>& pdrBytes)
{
	pldm_numeric_effecter_value_pdr pdr{};
	uint64_t field;

	pdrBytes.clear();

	pdrFromJson(record, pdr.hdr);
	pdrBytes.insert(pdrBytes.begin(), reinterpret_cast<uint8_t*>(&pdr.hdr.record_handle),
		reinterpret_cast<uint8_t*>(&pdr.hdr.record_handle) + sizeof(pdr.hdr.record_handle));
	pdrBytes.insert(pdrBytes.end(), (uint8_t)pdr.hdr.version);
	pdrBytes.insert(pdrBytes.end(), (uint8_t)pdr.hdr.type);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.hdr.record_change_num),
		reinterpret_cast<uint8_t*>(&pdr.hdr.record_change_num) + sizeof(pdr.hdr.record_change_num));
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.hdr.length),
		reinterpret_cast<uint8_t*>(&pdr.hdr.length) + sizeof(pdr.hdr.length));

	EXTRACT_FIELD_FROM_JSON(record, "PLDMTerminusHandler", pdr.terminus_handle);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.terminus_handle),
		reinterpret_cast<uint8_t*>(&pdr.terminus_handle) + sizeof(pdr.terminus_handle));

	EXTRACT_FIELD_FROM_JSON(record, "effecterID", pdr.effecter_id);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.effecter_id),
		reinterpret_cast<uint8_t*>(&pdr.effecter_id) + sizeof(pdr.effecter_id));

	EXTRACT_FIELD_FROM_JSON(record, "entityType", pdr.entity_type);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.entity_type),
		reinterpret_cast<uint8_t*>(&pdr.entity_type) + sizeof(pdr.entity_type));

	EXTRACT_FIELD_FROM_JSON(record, "entityInstanceNumber", pdr.entity_instance);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.entity_instance),
		reinterpret_cast<uint8_t*>(&pdr.entity_instance) + sizeof(pdr.entity_instance));

	EXTRACT_FIELD_FROM_JSON(record, "containerID", pdr.container_id);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.container_id),
		reinterpret_cast<uint8_t*>(&pdr.container_id) + sizeof(pdr.container_id));

	EXTRACT_FIELD_FROM_JSON(record, "effecterSemanticID", pdr.effecter_semantic_id);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.effecter_semantic_id),
		reinterpret_cast<uint8_t*>(&pdr.effecter_semantic_id) + sizeof(pdr.effecter_semantic_id));

	EXTRACT_FIELD_FROM_JSON(record, "effecterInit", pdr.effecter_init);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.effecter_init),
		reinterpret_cast<uint8_t*>(&pdr.effecter_init) + sizeof(pdr.effecter_init));

	EXTRACT_FIELD_FROM_JSON(record, "effecterAuxiliaryNamesPDR", pdr.effecter_auxiliary_names);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.effecter_auxiliary_names),
		reinterpret_cast<uint8_t*>(&pdr.effecter_auxiliary_names) + sizeof(pdr.effecter_auxiliary_names));

	EXTRACT_FIELD_FROM_JSON(record, "baseUnit", pdr.base_unit);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.base_unit),
		reinterpret_cast<uint8_t*>(&pdr.base_unit) + sizeof(pdr.base_unit));

	EXTRACT_FIELD_FROM_JSON(record, "unitModifier", pdr.unit_modifier);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.unit_modifier),
		reinterpret_cast<uint8_t*>(&pdr.unit_modifier) + sizeof(pdr.unit_modifier));

	EXTRACT_FIELD_FROM_JSON(record, "rateUnit", pdr.rate_unit);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.rate_unit),
		reinterpret_cast<uint8_t*>(&pdr.rate_unit) + sizeof(pdr.rate_unit));

	EXTRACT_FIELD_FROM_JSON(record, "baseOEMUnitHandle", pdr.base_oem_unit_handle);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.base_oem_unit_handle),
		reinterpret_cast<uint8_t*>(&pdr.base_oem_unit_handle) + sizeof(pdr.base_oem_unit_handle));

	EXTRACT_FIELD_FROM_JSON(record, "auxUnit", pdr.aux_unit);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.aux_unit),
		reinterpret_cast<uint8_t*>(&pdr.aux_unit) + sizeof(pdr.aux_unit));

	EXTRACT_FIELD_FROM_JSON(record, "auxUnitModifier", pdr.aux_unit_modifier);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.aux_unit_modifier),
		reinterpret_cast<uint8_t*>(&pdr.aux_unit_modifier) + sizeof(pdr.aux_unit_modifier));

	EXTRACT_FIELD_FROM_JSON(record, "auxRateUnit", pdr.aux_rate_unit);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.aux_rate_unit),
		reinterpret_cast<uint8_t*>(&pdr.aux_rate_unit) + sizeof(pdr.aux_rate_unit));

	EXTRACT_FIELD_FROM_JSON(record, "auxOEMUnitHandle", pdr.aux_oem_unit_handle);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.aux_oem_unit_handle),
		reinterpret_cast<uint8_t*>(&pdr.aux_oem_unit_handle) + sizeof(pdr.aux_oem_unit_handle));

	EXTRACT_FIELD_FROM_JSON(record, "isLinear", pdr.is_linear);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.is_linear),
		reinterpret_cast<uint8_t*>(&pdr.is_linear) + sizeof(pdr.is_linear));

	EXTRACT_FIELD_FROM_JSON(record, "effecterDataSize", pdr.effecter_data_size);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.effecter_data_size),
		reinterpret_cast<uint8_t*>(&pdr.effecter_data_size) + sizeof(pdr.effecter_data_size));

	EXTRACT_FIELD_FROM_JSON(record, "resolution", pdr.resolution);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.resolution),
		reinterpret_cast<uint8_t*>(&pdr.resolution) + sizeof(pdr.resolution));

	EXTRACT_FIELD_FROM_JSON(record, "offset", pdr.offset);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.offset),
		reinterpret_cast<uint8_t*>(&pdr.offset) + sizeof(pdr.offset));

	EXTRACT_FIELD_FROM_JSON(record, "accuracy", pdr.accuracy);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.accuracy),
		reinterpret_cast<uint8_t*>(&pdr.accuracy) + sizeof(pdr.accuracy));

	EXTRACT_FIELD_FROM_JSON(record, "plusTolerance", pdr.plus_tolerance);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.plus_tolerance),
		reinterpret_cast<uint8_t*>(&pdr.plus_tolerance) + sizeof(pdr.plus_tolerance));

	EXTRACT_FIELD_FROM_JSON(record, "minusTolerance", pdr.minus_tolerance);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.minus_tolerance),
		reinterpret_cast<uint8_t*>(&pdr.minus_tolerance) + sizeof(pdr.minus_tolerance));

	EXTRACT_FIELD_FROM_JSON(record, "stateTransitionInterval", pdr.state_transition_interval);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.state_transition_interval),
		reinterpret_cast<uint8_t*>(&pdr.state_transition_interval) + sizeof(pdr.state_transition_interval));

	EXTRACT_FIELD_FROM_JSON(record, "transitionInterval", pdr.transition_interval);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.transition_interval),
		reinterpret_cast<uint8_t*>(&pdr.transition_interval) + sizeof(pdr.transition_interval));

	EXTRACT_FIELD_FROM_JSON(record, "maxSettable", field);
	rangeFieldFromJson(field, field, pdrBytes);

	EXTRACT_FIELD_FROM_JSON(record, "minSettable", field);
	rangeFieldFromJson(field, field, pdrBytes);

	EXTRACT_FIELD_FROM_JSON(record, "rangeFieldFormat", pdr.range_field_format);
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.range_field_format),
		reinterpret_cast<uint8_t*>(&pdr.range_field_format) + sizeof(pdr.range_field_format));

	pdr.range_field_support.byte = record.value("rangeFieldSupport", 0); 
	pdrBytes.insert(pdrBytes.end(), reinterpret_cast<uint8_t*>(&pdr.range_field_support.byte),
		reinterpret_cast<uint8_t*>(&pdr.range_field_support.byte) + sizeof(pdr.range_field_support.byte));

	EXTRACT_FIELD_FROM_JSON(record, "nominalValue", field);
	rangeFieldFromJson(field, field, pdrBytes);

	EXTRACT_FIELD_FROM_JSON(record, "normalMax", field);
	rangeFieldFromJson(field, field, pdrBytes);

	EXTRACT_FIELD_FROM_JSON(record, "normalMin", field);
	rangeFieldFromJson(field, field, pdrBytes);

	EXTRACT_FIELD_FROM_JSON(record, "ratedMax", field);
	rangeFieldFromJson(field, field, pdrBytes);

	EXTRACT_FIELD_FROM_JSON(record, "ratedMin", field);
	rangeFieldFromJson(field, field, pdrBytes);
}

exec::task<int> PlatformManager::getPDRsFromJson(std::shared_ptr<Terminus> terminus)
{
    pldm_tid_t tid = terminus->getTid();

    /* Setting default values when getPDRRepositoryInfo fails or does not
     * support */
    uint8_t repositoryState = PLDM_AVAILABLE;
    uint32_t recordCount = std::numeric_limits<uint32_t>::max();
    uint32_t repositorySize = 0;
    uint32_t largestRecordSize = std::numeric_limits<uint32_t>::max();
    if (terminus->doesSupportCommand(PLDM_PLATFORM,
                                     PLDM_GET_PDR_REPOSITORY_INFO))
    {
        auto rc = co_await getPDRRepositoryInfo(tid, repositoryState,
                                                recordCount, repositorySize,
                                                largestRecordSize);
        if (rc)
        {
            lg2::error(
                "Failed to get PDR Repository Info for terminus with TID: {TID}, error: {ERROR}",
                "TID", tid, "ERROR", rc);
        }
        else
        {
            recordCount = std::min(recordCount + 1,
                                   std::numeric_limits<uint32_t>::max());
            largestRecordSize = std::min(largestRecordSize + 1,
                                         std::numeric_limits<uint32_t>::max());
        }
    }

    if (repositoryState != PLDM_AVAILABLE)
    {
        co_return PLDM_ERROR_NOT_READY;
    }

    std::ifstream jsonFile("/usr/share/pldm/pdr/com.amd.Hardware.Chassis.Model.sp7/amd_soc_pdr.json");
    auto jData = nlohmann::ordered_json::parse(jsonFile, nullptr, false);
    if (jData.is_discarded())
    {
        lg2::error("Parsing json file failed. File path {FILE_PATH}",
                   "FILE_PATH", std::string("jsonFile"));
        co_return PLDM_ERROR_NOT_READY;
    }
    for (const auto& jRecord: jData)
    {
        std::cout << "PDRType: " << jRecord["PDRType"] << std::endl;
        int pdrType = jRecord["PDRType"];
        pldm_terminus_locator_pdr terminusLocPdr  = {};
        pldm_state_sensor_pdr stateSensorPdr = {};
        std::vector<uint8_t> pdrBytes;
        switch (pdrType)
        {
            case PLDM_TERMINUS_LOCATOR_PDR:
                pdrFromJson(jRecord, terminusLocPdr);
                pdrBytes.resize(sizeof(pldm_terminus_locator_pdr));
                std::memcpy(pdrBytes.data(),
                            &terminusLocPdr, sizeof(pldm_terminus_locator_pdr));
                terminus->pdrs.push_back(pdrBytes);
                break;
            case PLDM_NUMERIC_SENSOR_PDR:
                numericPdrFromJson(jRecord, pdrBytes);
                terminus->pdrs.push_back(pdrBytes);
                break;
            case PLDM_STATE_SENSOR_PDR:
                pdrFromJson(jRecord, stateSensorPdr, pdrBytes);
                terminus->pdrs.push_back(pdrBytes);
                break;
            case PLDM_NUMERIC_EFFECTER_PDR:
                pdrBytes.resize(sizeof(pldm_numeric_effecter_value_pdr));
                numericEffecterValuePdrFromJson(jRecord, pdrBytes);
                terminus->pdrs.push_back(pdrBytes);
                break;
            case PLDM_ENTITY_AUXILIARY_NAMES_PDR:
                pdrBytes.resize(sizeof(pldm_compact_numeric_sensor_pdr));
                entityAuxNamesPdrFromJson(jRecord, pdrBytes);
                terminus->pdrs.push_back(pdrBytes);
		break;
            case PLDM_COMPACT_NUMERIC_SENSOR_PDR:
                pdrBytes.resize(sizeof(pldm_compact_numeric_sensor_pdr));
                compactNumericPdrFromJson(jRecord, pdrBytes);
                terminus->pdrs.push_back(pdrBytes);
                break;
            default:
                break;
        }
        std::cout<<"pdrBytes.size() = " << pdrBytes.size() << "\n";
        for (size_t i = 0; i < pdrBytes.size(); ++i) {
            std::cout << std::setw(2) << std::setfill('0') << std::hex << unsigned(pdrBytes[i]) << " ";
        }
        std::cout << "\n";
    }

    co_return PLDM_SUCCESS;
}

exec::task<int> PlatformManager::initTerminus()
{
    for (auto& [tid, terminus] : termini)
    {
        if (terminus->initialized)
        {
            continue;
        }

        /* Get Fru */
        uint16_t totalTableRecords = 0;
        if (terminus->doesSupportCommand(PLDM_FRU,
                                         PLDM_GET_FRU_RECORD_TABLE_METADATA))
        {
            auto rc =
                co_await getFRURecordTableMetadata(tid, &totalTableRecords);
            if (rc)
            {
                lg2::error(
                    "Failed to get FRU Metadata for terminus {TID}, error {ERROR}",
                    "TID", tid, "ERROR", rc);
            }
            if (!totalTableRecords)
            {
                lg2::info("Fru record table meta data has 0 records");
            }
        }

        std::vector<uint8_t> fruData{};
        if ((totalTableRecords != 0) &&
            terminus->doesSupportCommand(PLDM_FRU, PLDM_GET_FRU_RECORD_TABLE))
        {
            auto rc =
                co_await getFRURecordTables(tid, totalTableRecords, fruData);
            if (rc)
            {
                lg2::error(
                    "Failed to get Fru Record table for terminus {TID}, error {ERROR}",
                    "TID", tid, "ERROR", rc);
            }
        }

        if (terminus->doesSupportCommand(PLDM_PLATFORM, PLDM_GET_PDR))
        {
            auto rc = co_await getPDRs(terminus);
            if (rc)
            {
                lg2::error(
                    "Failed to fetch PDRs for terminus with TID: {TID}, error: {ERROR}",
                    "TID", tid, "ERROR", rc);
                continue; // Continue to next terminus
            }

            terminus->parseTerminusPDRs();
        }
        else
        {
            // read from JSON file
            auto rc = co_await getPDRsFromJson(terminus);
            if (rc)
            {
                lg2::error(
                    "Failed to fetch PDRs for terminus with TID from JSON: {TID}, error: {ERROR}",
                    "TID", tid, "ERROR", rc);
                continue; // Continue to next terminus
            }

            terminus->parseTerminusPDRs();
        }

        /**
         * Need terminus name from PDRs before updating Inventory object with
         * Fru data
         */
        if (fruData.size())
        {
            updateInventoryWithFru(tid, fruData.data(), fruData.size());
        }

        uint16_t terminusMaxBufferSize = terminus->maxBufferSize;
        if (!terminus->doesSupportCommand(PLDM_PLATFORM,
                                          PLDM_EVENT_MESSAGE_BUFFER_SIZE))
        {
            terminusMaxBufferSize = PLDM_PLATFORM_DEFAULT_MESSAGE_BUFFER_SIZE;
        }
        else
        {
            /* Get maxBufferSize use PLDM command eventMessageBufferSize */
            auto rc = co_await eventMessageBufferSize(
                tid, terminus->maxBufferSize, terminusMaxBufferSize);
            if (rc != PLDM_SUCCESS)
            {
                lg2::error(
                    "Failed to get message buffer size for terminus with TID: {TID}, error: {ERROR}",
                    "TID", tid, "ERROR", rc);
                terminusMaxBufferSize =
                    PLDM_PLATFORM_DEFAULT_MESSAGE_BUFFER_SIZE;
            }
        }
        terminus->maxBufferSize =
            std::min(terminus->maxBufferSize, terminusMaxBufferSize);

        auto rc = co_await configEventReceiver(tid);
        if (rc)
        {
            lg2::error(
                "Failed to config event receiver for terminus with TID: {TID}, error: {ERROR}",
                "TID", tid, "ERROR", rc);
        }
        terminus->initialized = true;

        const auto redfishResources = terminus->getRedfishResourcePdrsRaw();

        if (!redfishResources.empty())
        {
            auto info = terminusManager.getMctpInfoForTid(tid);
            if (info)
            {
                pldm::utils::emitRDEDeviceDetectedSignal(
                    tid, info->first, info->second, redfishResources);
            }
            else
            {
                lg2::error(
                    "Failed to find Mctp Info for terminus with TID: {TID}",
                    "TID", tid);
            }
        }

        if (manager)
        {
            manager->startSensorPolling(tid);
        }
        else
        {
            lg2::error(
                "Cannot start sensor polling for TID: {TID} because the manager is not initialized.",
                "TID", tid);
        }
    }

    co_return PLDM_SUCCESS;
}

exec::task<int> PlatformManager::configEventReceiver(pldm_tid_t tid)
{
    if (!termini.contains(tid))
    {
        co_return PLDM_ERROR;
    }

    auto& terminus = termini[tid];
    if (!terminus->doesSupportCommand(PLDM_PLATFORM,
                                      PLDM_EVENT_MESSAGE_SUPPORTED))
    {
        terminus->synchronyConfigurationSupported.byte =
            1 << PLDM_EVENT_MESSAGE_GLOBAL_ENABLE_ASYNC_KEEP_ALIVE;
    }
    else
    {
        /**
         *  Get synchronyConfigurationSupported use PLDM command
         *  eventMessageBufferSize
         */
        uint8_t synchronyConfiguration = 0;
        uint8_t numberEventClassReturned = 0;
        std::vector<uint8_t> eventClass{};
        auto rc = co_await eventMessageSupported(
            tid, 1, synchronyConfiguration,
            terminus->synchronyConfigurationSupported, numberEventClassReturned,
            eventClass);
        if (rc != PLDM_SUCCESS)
        {
            lg2::error(
                "Failed to get event message supported for terminus with TID: {TID}, error: {ERROR}",
                "TID", tid, "ERROR", rc);
            terminus->synchronyConfigurationSupported.byte = 0;
        }
    }

    if (!terminus->doesSupportCommand(PLDM_PLATFORM, PLDM_SET_EVENT_RECEIVER))
    {
        lg2::error("Terminus {TID} does not support Event", "TID", tid);
        co_return PLDM_ERROR;
    }

    /**
     *  Set Event receiver base on synchronyConfigurationSupported data
     *  use PLDM command SetEventReceiver
     */
    pldm_event_message_global_enable eventMessageGlobalEnable =
        PLDM_EVENT_MESSAGE_GLOBAL_DISABLE;
    uint16_t heartbeatTimer = 0;

    /* Use PLDM_EVENT_MESSAGE_GLOBAL_ENABLE_ASYNC_KEEP_ALIVE when
     * for eventMessageGlobalEnable when the terminus supports that type
     */
    if (terminus->synchronyConfigurationSupported.byte &
        (1 << PLDM_EVENT_MESSAGE_GLOBAL_ENABLE_ASYNC_KEEP_ALIVE))
    {
        heartbeatTimer = HEARTBEAT_TIMEOUT;
        eventMessageGlobalEnable =
            PLDM_EVENT_MESSAGE_GLOBAL_ENABLE_ASYNC_KEEP_ALIVE;
    }
    /* Use PLDM_EVENT_MESSAGE_GLOBAL_ENABLE_ASYNC when
     * for eventMessageGlobalEnable when the terminus does not support
     * PLDM_EVENT_MESSAGE_GLOBAL_ENABLE_ASYNC_KEEP_ALIVE
     * and supports PLDM_EVENT_MESSAGE_GLOBAL_ENABLE_ASYNC type
     */
    else if (terminus->synchronyConfigurationSupported.byte &
             (1 << PLDM_EVENT_MESSAGE_GLOBAL_ENABLE_ASYNC))
    {
        eventMessageGlobalEnable = PLDM_EVENT_MESSAGE_GLOBAL_ENABLE_ASYNC;
    }
    /* Only use PLDM_EVENT_MESSAGE_GLOBAL_ENABLE_POLLING
     * for eventMessageGlobalEnable when the terminus only supports
     * this type
     */
    else if (terminus->synchronyConfigurationSupported.byte &
             (1 << PLDM_EVENT_MESSAGE_GLOBAL_ENABLE_POLLING))
    {
        eventMessageGlobalEnable = PLDM_EVENT_MESSAGE_GLOBAL_ENABLE_POLLING;
    }

    if (eventMessageGlobalEnable != PLDM_EVENT_MESSAGE_GLOBAL_DISABLE)
    {
        auto rc = co_await setEventReceiver(tid, eventMessageGlobalEnable,
                                            PLDM_TRANSPORT_PROTOCOL_TYPE_MCTP,
                                            heartbeatTimer);
        if (rc != PLDM_SUCCESS)
        {
            lg2::error(
                "Failed to set event receiver for terminus with TID: {TID}, error: {ERROR}",
                "TID", tid, "ERROR", rc);
        }
    }

    co_return PLDM_SUCCESS;
}

exec::task<int> PlatformManager::getPDRs(std::shared_ptr<Terminus> terminus)
{
    pldm_tid_t tid = terminus->getTid();

    /* Setting default values when getPDRRepositoryInfo fails or does not
     * support */
    uint8_t repositoryState = PLDM_AVAILABLE;
    uint32_t recordCount = std::numeric_limits<uint32_t>::max();
    uint32_t repositorySize = 0;
    uint32_t largestRecordSize = std::numeric_limits<uint32_t>::max();
    if (terminus->doesSupportCommand(PLDM_PLATFORM,
                                     PLDM_GET_PDR_REPOSITORY_INFO))
    {
        auto rc =
            co_await getPDRRepositoryInfo(tid, repositoryState, recordCount,
                                          repositorySize, largestRecordSize);
        if (rc)
        {
            lg2::error(
                "Failed to get PDR Repository Info for terminus with TID: {TID}, error: {ERROR}",
                "TID", tid, "ERROR", rc);
        }
        else
        {
            recordCount =
                std::min(recordCount + 1, std::numeric_limits<uint32_t>::max());
            largestRecordSize = std::min(largestRecordSize + 1,
                                         std::numeric_limits<uint32_t>::max());
        }
    }

    if (repositoryState != PLDM_AVAILABLE)
    {
        co_return PLDM_ERROR_NOT_READY;
    }

    uint32_t recordHndl = 0;
    uint32_t nextRecordHndl = 0;
    uint32_t nextDataTransferHndl = 0;
    uint8_t transferFlag = 0;
    uint16_t responseCnt = 0;
    constexpr uint16_t recvBufSize = PLDM_PLATFORM_GETPDR_MAX_RECORD_BYTES;
    std::vector<uint8_t> recvBuf(recvBufSize);
    uint8_t transferCrc = 0;

    terminus->pdrs.clear();
    uint32_t receivedRecordCount = 0;

    do
    {
        auto rc =
            co_await getPDR(tid, recordHndl, 0, PLDM_GET_FIRSTPART, recvBufSize,
                            0, nextRecordHndl, nextDataTransferHndl,
                            transferFlag, responseCnt, recvBuf, transferCrc);

        if (rc)
        {
            lg2::error(
                "Failed to get PDRs for terminus {TID}, error: {RC}, first part of record handle {RECORD}",
                "TID", tid, "RC", rc, "RECORD", recordHndl);
            terminus->pdrs.clear();
            co_return rc;
        }

        if (transferFlag == PLDM_PLATFORM_TRANSFER_START_AND_END)
        {
            // single-part
            terminus->pdrs.emplace_back(std::vector<uint8_t>(
                recvBuf.begin(), recvBuf.begin() + responseCnt));
            recordHndl = nextRecordHndl;
        }
        else
        {
            // multipart transfer
            uint32_t receivedRecordSize = responseCnt;
            auto pdrHdr = new (recvBuf.data()) pldm_pdr_hdr;
            uint16_t recordChgNum = le16toh(pdrHdr->record_change_num);
            std::vector<uint8_t> receivedPdr(recvBuf.begin(),
                                             recvBuf.begin() + responseCnt);
            do
            {
                rc = co_await getPDR(
                    tid, recordHndl, nextDataTransferHndl, PLDM_GET_NEXTPART,
                    recvBufSize, recordChgNum, nextRecordHndl,
                    nextDataTransferHndl, transferFlag, responseCnt, recvBuf,
                    transferCrc);
                if (rc)
                {
                    lg2::error(
                        "Failed to get PDRs for terminus {TID}, error: {RC}, get middle part of record handle {RECORD}",
                        "TID", tid, "RC", rc, "RECORD", recordHndl);
                    terminus->pdrs.clear();
                    co_return rc;
                }

                receivedPdr.insert(receivedPdr.end(), recvBuf.begin(),
                                   recvBuf.begin() + responseCnt);
                receivedRecordSize += responseCnt;

                if (transferFlag == PLDM_PLATFORM_TRANSFER_END)
                {
                    terminus->pdrs.emplace_back(std::move(receivedPdr));
                    recordHndl = nextRecordHndl;
                }
            } while (nextDataTransferHndl != 0 &&
                     receivedRecordSize < largestRecordSize);
        }
        receivedRecordCount++;
    } while (nextRecordHndl != 0 && receivedRecordCount < recordCount);

    co_return PLDM_SUCCESS;
}

exec::task<int> PlatformManager::getPDR(
    const pldm_tid_t tid, const uint32_t recordHndl,
    const uint32_t dataTransferHndl, const uint8_t transferOpFlag,
    const uint16_t requestCnt, const uint16_t recordChgNum,
    uint32_t& nextRecordHndl, uint32_t& nextDataTransferHndl,
    uint8_t& transferFlag, uint16_t& responseCnt,
    std::vector<uint8_t>& recordData, uint8_t& transferCrc)
{
    Request request(sizeof(pldm_msg_hdr) + PLDM_GET_PDR_REQ_BYTES);
    auto requestMsg = new (request.data()) pldm_msg;
    auto rc = encode_get_pdr_req(0, recordHndl, dataTransferHndl,
                                 transferOpFlag, requestCnt, recordChgNum,
                                 requestMsg, PLDM_GET_PDR_REQ_BYTES);
    if (rc)
    {
        lg2::error(
            "Failed to encode request GetPDR for terminus ID {TID}, error {RC} ",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    const pldm_msg* responseMsg = nullptr;
    size_t responseLen = 0;
    rc = co_await terminusManager.sendRecvPldmMsg(tid, request, &responseMsg,
                                                  &responseLen);
    if (rc)
    {
        lg2::error(
            "Failed to send GetPDR message for terminus {TID}, error {RC}",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    uint8_t completionCode;
    rc = decode_get_pdr_resp(responseMsg, responseLen, &completionCode,
                             &nextRecordHndl, &nextDataTransferHndl,
                             &transferFlag, &responseCnt, recordData.data(),
                             recordData.size(), &transferCrc);
    if (rc)
    {
        lg2::error(
            "Failed to decode response GetPDR for terminus ID {TID}, error {RC} ",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    if (completionCode != PLDM_SUCCESS)
    {
        lg2::error("Error : GetPDR for terminus ID {TID}, complete code {CC}.",
                   "TID", tid, "CC", completionCode);
        co_return rc;
    }

    co_return completionCode;
}

exec::task<int> PlatformManager::getPDRRepositoryInfo(
    const pldm_tid_t tid, uint8_t& repositoryState, uint32_t& recordCount,
    uint32_t& repositorySize, uint32_t& largestRecordSize)
{
    Request request(sizeof(pldm_msg_hdr) + sizeof(uint8_t));
    auto requestMsg = new (request.data()) pldm_msg;
    auto rc = encode_pldm_header_only(PLDM_REQUEST, 0, PLDM_PLATFORM,
                                      PLDM_GET_PDR_REPOSITORY_INFO, requestMsg);
    if (rc)
    {
        lg2::error(
            "Failed to encode request GetPDRRepositoryInfo for terminus ID {TID}, error {RC} ",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    const pldm_msg* responseMsg = nullptr;
    size_t responseLen = 0;
    rc = co_await terminusManager.sendRecvPldmMsg(tid, request, &responseMsg,
                                                  &responseLen);
    if (rc)
    {
        lg2::error(
            "Failed to send GetPDRRepositoryInfo message for terminus {TID}, error {RC}",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    uint8_t completionCode = 0;
    std::array<uint8_t, PLDM_TIMESTAMP104_SIZE> updateTime = {};
    std::array<uint8_t, PLDM_TIMESTAMP104_SIZE> oemUpdateTime = {};
    uint8_t dataTransferHandleTimeout = 0;

    rc = decode_get_pdr_repository_info_resp(
        responseMsg, responseLen, &completionCode, &repositoryState,
        updateTime.data(), oemUpdateTime.data(), &recordCount, &repositorySize,
        &largestRecordSize, &dataTransferHandleTimeout);
    if (rc)
    {
        lg2::error(
            "Failed to decode response GetPDRRepositoryInfo for terminus ID {TID}, error {RC} ",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    if (completionCode != PLDM_SUCCESS)
    {
        lg2::error(
            "Error : GetPDRRepositoryInfo for terminus ID {TID}, complete code {CC}.",
            "TID", tid, "CC", completionCode);
        co_return rc;
    }

    co_return completionCode;
}

exec::task<int> PlatformManager::eventMessageBufferSize(
    pldm_tid_t tid, uint16_t receiverMaxBufferSize,
    uint16_t& terminusBufferSize)
{
    Request request(
        sizeof(pldm_msg_hdr) + PLDM_EVENT_MESSAGE_BUFFER_SIZE_REQ_BYTES);
    auto requestMsg = new (request.data()) pldm_msg;
    auto rc = encode_event_message_buffer_size_req(0, receiverMaxBufferSize,
                                                   requestMsg);
    if (rc)
    {
        lg2::error(
            "Failed to encode request GetPDRRepositoryInfo for terminus ID {TID}, error {RC} ",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    const pldm_msg* responseMsg = nullptr;
    size_t responseLen = 0;
    rc = co_await terminusManager.sendRecvPldmMsg(tid, request, &responseMsg,
                                                  &responseLen);
    if (rc)
    {
        lg2::error(
            "Failed to send EventMessageBufferSize message for terminus {TID}, error {RC}",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    uint8_t completionCode;
    rc = decode_event_message_buffer_size_resp(
        responseMsg, responseLen, &completionCode, &terminusBufferSize);
    if (rc)
    {
        lg2::error(
            "Failed to decode response EventMessageBufferSize for terminus ID {TID}, error {RC} ",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    if (completionCode != PLDM_SUCCESS)
    {
        lg2::error(
            "Error : EventMessageBufferSize for terminus ID {TID}, complete code {CC}.",
            "TID", tid, "CC", completionCode);
        co_return completionCode;
    }

    co_return completionCode;
}

exec::task<int> PlatformManager::setEventReceiver(
    pldm_tid_t tid, pldm_event_message_global_enable eventMessageGlobalEnable,
    pldm_transport_protocol_type protocolType, uint16_t heartbeatTimer)
{
    size_t requestBytes = PLDM_SET_EVENT_RECEIVER_REQ_BYTES;
    /**
     * Ignore heartbeatTimer bytes when eventMessageGlobalEnable is not
     * ENABLE_ASYNC_KEEP_ALIVE
     */
    if (eventMessageGlobalEnable !=
        PLDM_EVENT_MESSAGE_GLOBAL_ENABLE_ASYNC_KEEP_ALIVE)
    {
        requestBytes = requestBytes - sizeof(heartbeatTimer);
    }
    Request request(sizeof(pldm_msg_hdr) + requestBytes);
    auto requestMsg = new (request.data()) pldm_msg;
    auto rc = encode_set_event_receiver_req(
        0, eventMessageGlobalEnable, protocolType,
        terminusManager.getLocalEid(), heartbeatTimer, requestMsg);
    if (rc)
    {
        lg2::error(
            "Failed to encode request SetEventReceiver for terminus ID {TID}, error {RC} ",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    const pldm_msg* responseMsg = nullptr;
    size_t responseLen = 0;
    rc = co_await terminusManager.sendRecvPldmMsg(tid, request, &responseMsg,
                                                  &responseLen);
    if (rc)
    {
        lg2::error(
            "Failed to send SetEventReceiver message for terminus {TID}, error {RC}",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    uint8_t completionCode;
    rc = decode_set_event_receiver_resp(responseMsg, responseLen,
                                        &completionCode);
    if (rc)
    {
        lg2::error(
            "Failed to decode response SetEventReceiver for terminus ID {TID}, error {RC} ",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    if (completionCode != PLDM_SUCCESS)
    {
        lg2::error(
            "Error : SetEventReceiver for terminus ID {TID}, complete code {CC}.",
            "TID", tid, "CC", completionCode);
        co_return completionCode;
    }

    co_return completionCode;
}

exec::task<int> PlatformManager::eventMessageSupported(
    pldm_tid_t tid, uint8_t formatVersion, uint8_t& synchronyConfiguration,
    bitfield8_t& synchronyConfigurationSupported,
    uint8_t& numberEventClassReturned, std::vector<uint8_t>& eventClass)
{
    Request request(
        sizeof(pldm_msg_hdr) + PLDM_EVENT_MESSAGE_SUPPORTED_REQ_BYTES);
    auto requestMsg = new (request.data()) pldm_msg;
    auto rc = encode_event_message_supported_req(0, formatVersion, requestMsg);
    if (rc)
    {
        lg2::error(
            "Failed to encode request EventMessageSupported for terminus ID {TID}, error {RC} ",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    const pldm_msg* responseMsg = nullptr;
    size_t responseLen = 0;
    rc = co_await terminusManager.sendRecvPldmMsg(tid, request, &responseMsg,
                                                  &responseLen);
    if (rc)
    {
        lg2::error(
            "Failed to send EventMessageSupported message for terminus {TID}, error {RC}",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    uint8_t completionCode = 0;
    uint8_t eventClassCount = static_cast<uint8_t>(responseLen) -
                              PLDM_EVENT_MESSAGE_SUPPORTED_MIN_RESP_BYTES;
    eventClass.resize(eventClassCount);

    rc = decode_event_message_supported_resp(
        responseMsg, responseLen, &completionCode, &synchronyConfiguration,
        &synchronyConfigurationSupported, &numberEventClassReturned,
        eventClass.data(), eventClassCount);
    if (rc)
    {
        lg2::error(
            "Failed to decode response EventMessageSupported for terminus ID {TID}, error {RC} ",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    if (completionCode != PLDM_SUCCESS)
    {
        lg2::error(
            "Error : EventMessageSupported for terminus ID {TID}, complete code {CC}.",
            "TID", tid, "CC", completionCode);
        co_return completionCode;
    }

    co_return completionCode;
}

exec::task<int> PlatformManager::getFRURecordTableMetadata(pldm_tid_t tid,
                                                           uint16_t* total)
{
    Request request(
        sizeof(pldm_msg_hdr) + PLDM_GET_FRU_RECORD_TABLE_METADATA_REQ_BYTES);
    auto requestMsg = new (request.data()) pldm_msg;

    auto rc = encode_get_fru_record_table_metadata_req(
        0, requestMsg, PLDM_GET_FRU_RECORD_TABLE_METADATA_REQ_BYTES);
    if (rc)
    {
        lg2::error(
            "Failed to encode request GetFRURecordTableMetadata for terminus ID {TID}, error {RC} ",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    const pldm_msg* responseMsg = nullptr;
    size_t responseLen = 0;

    rc = co_await terminusManager.sendRecvPldmMsg(tid, request, &responseMsg,
                                                  &responseLen);
    if (rc)
    {
        lg2::error(
            "Failed to send GetFRURecordTableMetadata message for terminus {TID}, error {RC}",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    uint8_t completionCode = 0;
    if (responseMsg == nullptr || !responseLen)
    {
        lg2::error(
            "No response data for GetFRURecordTableMetadata for terminus {TID}",
            "TID", tid);
        co_return rc;
    }

    uint8_t fru_data_major_version, fru_data_minor_version;
    uint32_t fru_table_maximum_size, fru_table_length;
    uint16_t total_record_set_identifiers;
    uint32_t checksum;
    rc = decode_get_fru_record_table_metadata_resp(
        responseMsg, responseLen, &completionCode, &fru_data_major_version,
        &fru_data_minor_version, &fru_table_maximum_size, &fru_table_length,
        &total_record_set_identifiers, total, &checksum);

    if (rc)
    {
        lg2::error(
            "Failed to decode response GetFRURecordTableMetadata for terminus ID {TID}, error {RC} ",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    if (completionCode != PLDM_SUCCESS)
    {
        lg2::error(
            "Error : GetFRURecordTableMetadata for terminus ID {TID}, complete code {CC}.",
            "TID", tid, "CC", completionCode);
        co_return rc;
    }

    co_return rc;
}

exec::task<int> PlatformManager::getFRURecordTable(
    pldm_tid_t tid, const uint32_t dataTransferHndl,
    const uint8_t transferOpFlag, uint32_t* nextDataTransferHndl,
    uint8_t* transferFlag, size_t* responseCnt,
    std::vector<uint8_t>& recordData)
{
    Request request(sizeof(pldm_msg_hdr) + PLDM_GET_FRU_RECORD_TABLE_REQ_BYTES);
    auto requestMsg = new (request.data()) pldm_msg;

    auto rc = encode_get_fru_record_table_req(
        0, dataTransferHndl, transferOpFlag, requestMsg,
        PLDM_GET_FRU_RECORD_TABLE_REQ_BYTES);
    if (rc != PLDM_SUCCESS)
    {
        lg2::error(
            "Failed to encode request GetFRURecordTable for terminus ID {TID}, error {RC} ",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    const pldm_msg* responseMsg = nullptr;
    size_t responseLen = 0;

    rc = co_await terminusManager.sendRecvPldmMsg(tid, request, &responseMsg,
                                                  &responseLen);
    if (rc)
    {
        lg2::error(
            "Failed to send GetFRURecordTable message for terminus {TID}, error {RC}",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    uint8_t completionCode = 0;
    if (responseMsg == nullptr || !responseLen)
    {
        lg2::error("No response data for GetFRURecordTable for terminus {TID}",
                   "TID", tid);
        co_return rc;
    }

    auto responsePtr = reinterpret_cast<const struct pldm_msg*>(responseMsg);
    rc = decode_get_fru_record_table_resp(
        responsePtr, responseLen - sizeof(pldm_msg_hdr), &completionCode,
        nextDataTransferHndl, transferFlag, recordData.data(), responseCnt);

    if (rc)
    {
        lg2::error(
            "Failed to decode response GetFRURecordTable for terminus ID {TID}, error {RC} ",
            "TID", tid, "RC", rc);
        co_return rc;
    }

    if (completionCode != PLDM_SUCCESS)
    {
        lg2::error(
            "Error : GetFRURecordTable for terminus ID {TID}, complete code {CC}.",
            "TID", tid, "CC", completionCode);
        co_return rc;
    }

    co_return rc;
}

void PlatformManager::updateInventoryWithFru(
    pldm_tid_t tid, const uint8_t* fruData, const size_t fruLen)
{
    if (tid == PLDM_TID_RESERVED || !termini.contains(tid) || !termini[tid])
    {
        lg2::error("Invalid terminus {TID}", "TID", tid);
        return;
    }

    termini[tid]->updateInventoryWithFru(fruData, fruLen);
}

exec::task<int> PlatformManager::getFRURecordTables(
    pldm_tid_t tid, const uint16_t& totalTableRecords,
    std::vector<uint8_t>& fruData)
{
    if (!totalTableRecords)
    {
        lg2::info("Fru record table has 0 records");
        co_return PLDM_ERROR;
    }

    uint32_t dataTransferHndl = 0;
    uint32_t nextDataTransferHndl = 0;
    uint8_t transferFlag = 0;
    uint8_t transferOpFlag = PLDM_GET_FIRSTPART;
    size_t responseCnt = 0;
    std::vector<uint8_t> recvBuf(PLDM_PLATFORM_GETPDR_MAX_RECORD_BYTES);

    size_t fruLength = 0;
    std::vector<uint8_t> receivedFru(0);
    do
    {
        auto rc = co_await getFRURecordTable(
            tid, dataTransferHndl, transferOpFlag, &nextDataTransferHndl,
            &transferFlag, &responseCnt, recvBuf);

        if (rc)
        {
            lg2::error(
                "Failed to get Fru Record Data for terminus {TID}, error: {RC}, first part of data handle {RECORD}",
                "TID", tid, "RC", rc, "RECORD", dataTransferHndl);
            co_return rc;
        }

        receivedFru.insert(receivedFru.end(), recvBuf.begin(),
                           recvBuf.begin() + responseCnt);
        fruLength += responseCnt;
        if (transferFlag == PLDM_PLATFORM_TRANSFER_START_AND_END ||
            transferFlag == PLDM_PLATFORM_TRANSFER_END)
        {
            break;
        }

        // multipart transfer
        dataTransferHndl = nextDataTransferHndl;
        transferOpFlag = PLDM_GET_NEXTPART;

    } while (nextDataTransferHndl != 0);

    if (fruLength != receivedFru.size())
    {
        lg2::error(
            "Size of Fru Record Data {SIZE} for terminus {TID} is different the responded size {RSPSIZE}.",
            "SIZE", receivedFru.size(), "RSPSIZE", fruLength);
        co_return PLDM_ERROR_INVALID_LENGTH;
    }

    fruData = receivedFru;

    co_return PLDM_SUCCESS;
}

} // namespace platform_mc
} // namespace pldm
