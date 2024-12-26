#pragma once

#include "srtc/srtc.h"

#include <memory>
#include <mutex>

namespace srtc {

class SdpAnswer;
class SdpOffer;
class Track;

class PeerConnection {
public:
    PeerConnection() = default;
    ~PeerConnection() = default;

    void setSdpOffer(const std::shared_ptr<SdpOffer>& offer);
    void setSdpAnswer(const std::shared_ptr<SdpAnswer>& answer);

    std::shared_ptr<SdpOffer> getSdpOffer() const;
    std::shared_ptr<SdpAnswer> getSdpAnswer() const;

    std::shared_ptr<Track> getVideoTrack() const;
    std::shared_ptr<Track> getAudioTrack() const;

private:
    mutable std::mutex mMutex;

    std::shared_ptr<SdpOffer> mSdpOffer SRTC_GUARDED_BY(mMutex);
    std::shared_ptr<SdpAnswer> mSdpAnswer SRTC_GUARDED_BY(mMutex);

    std::shared_ptr<Track> mVideoTrack SRTC_GUARDED_BY(mMutex);
    std::shared_ptr<Track> mAudioTrack SRTC_GUARDED_BY(mMutex);
};

}
