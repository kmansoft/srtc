#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/srtc.h"
#include "srtc/error.h"
#include "srtc/extension_map.h"
#include "srtc/x509_hash.h"

#include <string>
#include <memory>
#include <vector>

namespace srtc {

class SdpOffer;
class Track;

class SdpAnswer {
public:

    class TrackSelector {
    public:
        virtual ~TrackSelector() = default;

        [[nodiscard]] virtual std::shared_ptr<Track> selectTrack(MediaType type,
                                                                 const std::vector<std::shared_ptr<Track>>& list) const = 0;
    };

    static Error parse(const std::shared_ptr<SdpOffer>& offer,
                       const std::string& answer,
                       const std::shared_ptr<TrackSelector>& selector,
                       std::shared_ptr<SdpAnswer>& outAnswer);

    ~SdpAnswer();

    [[nodiscard]] std::string getIceUFrag() const;
    [[nodiscard]] std::string getIcePassword() const;
    [[nodiscard]] std::vector<Host> getHostList() const;
    [[nodiscard]] std::shared_ptr<Track> getVideoTrack() const;
    [[nodiscard]] std::shared_ptr<Track> getAudioTrack() const;
    [[nodiscard]] const ExtensionMap& getVideoExtensionMap() const;
    [[nodiscard]] const ExtensionMap& getAudioExtensionMap() const;
    [[nodiscard]] bool isSetupActive() const;
    [[nodiscard]] const X509Hash& getCertificateHash() const;

private:
    const std::string mIceUFrag;
    const std::string mIcePassword;
    const std::vector<Host> mHostList;
    const std::shared_ptr<Track> mVideoTrack;
    const std::shared_ptr<Track> mAudioTrack;
    const ExtensionMap mVideoExtensionMap;
    const ExtensionMap mAudioExtensionMap;
    const bool mIsSetupActive;
    const X509Hash mCertHash;

    SdpAnswer(const std::string& iceUFrag,
              const std::string& icePassword,
              const std::vector<Host>& hostList,
              const std::shared_ptr<Track>& videoTrack,
              const std::shared_ptr<Track>& audioTrack,
              const ExtensionMap& videoExtensionMap,
              const ExtensionMap& audioExtensionMap,
              bool isSetupActive,
              const X509Hash& certHash);
};

}
