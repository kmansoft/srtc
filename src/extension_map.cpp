#include "srtc/extension_map.h"

namespace srtc
{

ExtensionMap::ExtensionMap()
    : mLastId(0)
{
}

ExtensionMap::~ExtensionMap() = default;

void ExtensionMap::add(uint8_t id, const std::string& name)
{
    mEntryList.emplace_back(id, name);
}

uint8_t ExtensionMap::findByName(const std::string& name) const
{
    if (mLastName == name && mLastId != 0) {
        return mLastId;
    }

    for (const auto& entry : mEntryList) {
        if (entry.name == name) {
            mLastName = name;
            mLastId = entry.id;

            return entry.id;
        }
    }

    return 0;
}

std::string ExtensionMap::findById(uint8_t id) const
{
    if (mLastId == id && !mLastName.empty()) {
        return mLastName;
    }

    for (const auto& entry : mEntryList) {
        if (entry.id == id) {
            mLastName = entry.name;
            mLastId = entry.id;

            return entry.name;
        }
    }

    return {};
}

void ExtensionMap::clear()
{
    mLastName.clear();
    mLastId = 0;
    mEntryList.clear();
}

} // namespace srtc
