#include "srtc/extension_map.h"

namespace srtc
{

void ExtensionMap::add(uint8_t id, const std::string& name)
{
    mEntryList.emplace_back(id, name);
}

uint8_t ExtensionMap::findByName(const std::string& name) const
{
    for (const auto& entry : mEntryList) {
        if (entry.name == name) {
            return entry.id;
        }
    }

    return 0;
}

std::string ExtensionMap::findById(uint8_t id) const
{
    for (const auto& entry : mEntryList) {
        if (entry.id == id) {
            return entry.name;
        }
    }

    return {};
}

} // namespace srtc
