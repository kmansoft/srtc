#pragma once

#include <string>
#include <unordered_map>

namespace srtc {

class ExtensionMap {
public:
    ExtensionMap() = default;
    ~ExtensionMap() = default;

    void set(int id, const std::string& name);

private:
    std::unordered_map<int, std::string> mMapIdToName;
    std::unordered_map<std::string, int> mMapNameToId;
};

}
