#include "srtc/peer_connection.h"
#include "srtc/sdp_answer.h"
#include "srtc/track.h"

namespace srtc {

void PeerConnection::setSdpOffer(const std::shared_ptr<SdpOffer>& offer)
{
    std::lock_guard lock(mMutex);
    mSdpOffer = offer;
}

void PeerConnection::setSdpAnswer(const std::shared_ptr<SdpAnswer>& answer)
{
    std::lock_guard lock(mMutex);
    mSdpAnswer = answer;

    mVideoTrack = answer->getVideoTrack();
    mAudioTrack = answer->getAudioTrack();
}

std::shared_ptr<SdpOffer> PeerConnection::getSdpOffer() const
{
    std::lock_guard lock(mMutex);
    return mSdpOffer;
}

std::shared_ptr<SdpAnswer> PeerConnection::getSdpAnswer() const
{
    std::lock_guard lock(mMutex);
    return mSdpAnswer;
}

std::shared_ptr<Track> PeerConnection::getVideoTrack() const
{
    std::lock_guard lock(mMutex);
    return mVideoTrack;
}

std::shared_ptr<Track> PeerConnection::getAudioTrack() const
{
    std::lock_guard lock(mMutex);
    return mAudioTrack;
}

}
