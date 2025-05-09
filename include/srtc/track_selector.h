#pragma once

#include "srtc/srtc.h"

#include <memory>
#include <vector>

namespace srtc
{

class Track;

class TrackSelector
{
public:
    virtual ~TrackSelector() = default;

    [[nodiscard]] virtual std::shared_ptr<Track> selectTrack(MediaType type,
                                                             const std::vector<std::shared_ptr<Track>>& list) const = 0;
};

class HighestTrackSelector : public srtc::TrackSelector
{
public:
    ~HighestTrackSelector() override = default;

    [[nodiscard]] std::shared_ptr<srtc::Track> selectTrack(
        srtc::MediaType type, const std::vector<std::shared_ptr<srtc::Track>>& list) const override;
};

} // namespace srtc
