#include "srtc/track_selector.h"
#include "srtc/media.h"
#include "srtc/track.h"

namespace
{

bool isBetter(const std::shared_ptr<srtc::Track>& best, const std::shared_ptr<srtc::Track>& curr)
{
    if (!best) {
        return true;
    }

    if (best->getCodec() != curr->getCodec()) {
        return best->getCodec() < curr->getCodec();
    }

    const auto best_options = best->getCodecOptions();
    const auto curr_options = curr->getCodecOptions();

    const auto best_profileId = best_options ? best_options->profileLevelId : 0;
    const auto curr_profileId = curr_options ? curr_options->profileLevelId : 0;

    return best_profileId < curr_profileId;
}

} // namespace

namespace srtc
{

std::shared_ptr<Track> HighestTrackSelector::selectTrack(const std::vector<std::shared_ptr<Track>>& list) const
{
    if (list.empty()) {
        return nullptr;
    }

    // All tracks must have the same media
    const auto type = list[0]->getMediaType();

    if (type == MediaType::Audio) {
        return list[0];
    }

    if (type == MediaType::Video) {
        std::shared_ptr<Track> best;
        for (const auto& curr : list) {
            if (isBetter(best, curr)) {
                best = curr;
            }
        }
        return best;
    }

    return {};
}

} // namespace srtc