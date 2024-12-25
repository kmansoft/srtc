#pragma once

#include <memory>
#include <mutex>

namespace srtc {

class SdpOffer;

class PeerConnection {
public:
    PeerConnection() = default;
    ~PeerConnection() = default;

    void setSdpOffer(const std::shared_ptr<SdpOffer>& offer);

private:
    std::mutex mMutex;
    std::shared_ptr<SdpOffer> mSdpOffer;
};

}
