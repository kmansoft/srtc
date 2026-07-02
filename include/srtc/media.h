#pragma once

#include "srtc/srtc.h"
#include "srtc/extension_map.h"

#include <string>

namespace srtc
{

class Media
{
public:
    Media(const std::string& id, MediaType type);
    Media(const std::string& id, MediaType type, const ExtensionMap& extensionMap);
    ~Media();

    [[nodiscard]] std::string getId() const;
    [[nodiscard]] MediaType getType() const;
    [[nodiscard]] const ExtensionMap& getExtensionMap() const;

private:
    const std::string mId;
    const MediaType mType;
    const ExtensionMap mExtensionMap;
};

} // namespace srtc