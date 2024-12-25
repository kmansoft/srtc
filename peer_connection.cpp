#include "srtc/peer_connection.h"

namespace srtc {

void PeerConnection::setSdpOffer(const std::shared_ptr<SdpOffer>& offer)
{
    std::lock_guard lock(mMutex);
    mSdpOffer = offer;
}

}
