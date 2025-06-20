#include "srtc/track_selector.h"
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

std::shared_ptr<srtc::Track> HighestTrackSelector::selectTrack(
    srtc::MediaType type, const std::vector<std::shared_ptr<srtc::Track>>& list) const
{
    if (list.empty()) {
        return nullptr;
    }

    if (type == srtc::MediaType::Audio) {
        return list[0];
    } else if (type == srtc::MediaType::Video) {
        std::shared_ptr<srtc::Track> best;
        for (const auto& curr : list) {
            if (isBetter(best, curr)) {
                best = curr;
            }
        }
        return best;
    } else {
        return nullptr;
    }
}

} // namespace srtc