#pragma once

#include "srtc/srtc.h"

#include <string>

namespace srtc
{

class Media
{
public:
    Media(const std::string& id, MediaType type);
    ~Media();

    [[nodiscard]] std::string getId() const;
    [[nodiscard]] MediaType getType() const;

private:
    const std::string mId;
    const MediaType mType;
};

} // namespace srtc