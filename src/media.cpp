#include "srtc/media.h"

namespace srtc
{

Media::Media(const std::string& id, MediaType type)
    : mId(id)
    , mType(type)
{
}

Media::Media(const std::string& id, MediaType type, const ExtensionMap& extensionMap)
    : mId(id)
    , mType(type)
    , mExtensionMap(extensionMap)
{
}

Media::~Media()
{
}

std::string Media::getId() const
{
    return mId;
}

MediaType Media::getType() const
{
    return mType;
}

const ExtensionMap& Media::getExtensionMap() const
{
    return mExtensionMap;
}

} // namespace srtc