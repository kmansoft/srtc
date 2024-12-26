#include "srtc/extension_map.h"

namespace srtc {

void ExtensionMap::set(int id, const std::string& name)
{
    mMapIdToName.emplace(id, name);
    mMapNameToId.emplace(name, id);
}

}
