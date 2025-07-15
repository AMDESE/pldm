#include "dictionary_manager.hpp"

#include <stdexcept>

namespace pldm::rde
{

Dictionary& DictionaryManager::getOrCreate(uint32_t resourceId,
                                           uint8_t schemaClass)
{
    DictionaryKey key{resourceId, schemaClass};
    auto [it, inserted] = dictionaries.emplace(
        key, Dictionary(resourceId, schemaClass, deviceUUID));
    return it->second;
}

void DictionaryManager::addChunk(uint32_t resourceId, uint8_t schemaClass,
                                 std::span<const uint8_t> payload,
                                 bool hasChecksum, bool isFinalChunk)
{
    if (payload.empty())
    {
        throw std::invalid_argument("Payload chunk is empty.");
    }

    Dictionary& dict = getOrCreate(resourceId, schemaClass);

    if (!dict.addToDictionaryBytes(payload, hasChecksum))
    {
        throw std::runtime_error("Failed to add chunk to dictionary.");
    }

    if (isFinalChunk)
    {
        dict.markComplete();
        try
        {
            dict.save();
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error(
                std::string("Failed to persist dictionary: ") + e.what());
        }
    }
}

void DictionaryManager::reset(uint32_t resourceId, uint8_t schemaClass)
{
    DictionaryKey key{resourceId, schemaClass};
    auto it = dictionaries.find(key);
    if (it != dictionaries.end())
    {
        it->second.reset();
        dictionaries.erase(it);
    }
}

const Dictionary* DictionaryManager::get(uint32_t resourceId,
                                         uint8_t schemaClass) const
{
    DictionaryKey key{resourceId, schemaClass};
    auto it = dictionaries.find(key);
    return (it != dictionaries.end()) ? &it->second : nullptr;
}

void DictionaryManager::buildAnnotationDictionary(const std::string& filePath)
{
    Dictionary dict(0, 0, deviceUUID);
    dict.loadFromFile(filePath);
    dict.save();
    annotationDictionary = std::move(dict);
}

const Dictionary* DictionaryManager::getAnnotationDictionary() const
{
    return annotationDictionary ? &(*annotationDictionary) : nullptr;
}

void DictionaryManager::createDictionaryFromFile(
    uint32_t resourceId, uint8_t schemaClass, const std::string& filePath)
{
    Dictionary& dict = getOrCreate(resourceId, schemaClass);
    dict.loadFromFile(filePath);
    dict.save();
}
} // namespace pldm::rde
