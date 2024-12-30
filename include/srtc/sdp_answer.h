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

class Track;

class SdpAnswer {
public:

    static Error parse(const std::string& answer, std::shared_ptr<SdpAnswer>& outAnswer);

    ~SdpAnswer();

    [[nodiscard]] std::string getIceUFrag() const;
    [[nodiscard]] std::string getIcePassword() const;
    [[nodiscard]] ExtensionMap getExtensionMap() const;
    [[nodiscard]] std::vector<Host> getHostList() const;
    [[nodiscard]] std::shared_ptr<Track> getVideoTrack() const;
    [[nodiscard]] std::shared_ptr<Track> getAudioTrack() const;
    [[nodiscard]] bool isSetupActive() const;
    [[nodiscard]] const X509Hash& getCertificateHash() const;

private:
    const std::string mIceUFrag;
    const std::string mIcePassword;
    const ExtensionMap mExtensionMap;
    const std::vector<Host> mHostList;
    const std::shared_ptr<Track> mVideoTrack;
    const std::shared_ptr<Track> mAudioTrack;
    const bool mIsSetupActive;
    const X509Hash mCertHash;

    SdpAnswer(const std::string& iceUFrag,
              const std::string& icePassword,
              const ExtensionMap& extensionMap,
              const std::vector<Host>& hostList,
              const std::shared_ptr<Track>& videoTrack,
              const std::shared_ptr<Track>& audioTrack,
              bool isSetupActive,
              const X509Hash& certHash);
};

}
