#pragma once

#include "srtc/srtc.h"

#include <memory>
#include <vector>

namespace srtc
{

class Media;
class Track;

class TrackSelector
{
public:
    virtual ~TrackSelector() = default;

    [[nodiscard]] virtual std::shared_ptr<Track> selectTrack(const std::vector<std::shared_ptr<Track>>& list) const = 0;
};

class HighestTrackSelector : public TrackSelector
{
public:
    ~HighestTrackSelector() override = default;

    [[nodiscard]] std::shared_ptr<Track> selectTrack(const std::vector<std::shared_ptr<Track>>& list) const override;
};

} // namespace srtc
