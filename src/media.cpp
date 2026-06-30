#include "srtc/media.h"

namespace srtc
{

Media::Media(const std::string& id, MediaType type)
    : mId(id)
    , mType(type)
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

} // namespace srtc