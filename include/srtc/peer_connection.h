#pragma once

#include <memory>
#include <mutex>

namespace srtc {

class SdpAnswer;
class SdpOffer;

class PeerConnection {
public:
    PeerConnection() = default;
    ~PeerConnection() = default;

    void setSdpOffer(const std::shared_ptr<SdpOffer>& offer);
    void setSdpAnswer(const std::shared_ptr<SdpAnswer>& answer);

private:
    std::mutex mMutex;
    std::shared_ptr<SdpOffer> mSdpOffer;
    std::shared_ptr<SdpAnswer> mSdpAnswer;
};

}
