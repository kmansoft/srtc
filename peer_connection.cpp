#include "srtc/peer_connection.h"
#include <jni.h>

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
}

}
