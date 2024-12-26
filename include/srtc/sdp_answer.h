#pragma once

#include "srtc/srtc.h"
#include "srtc/error.h"
#include "srtc/extension_map.h"

#include <string>
#include <memory>
#include <vector>

namespace srtc {

class Track;

class SdpAnswer {
public:

    static Error parse(const std::string& answer, std::shared_ptr<SdpAnswer>& outAnswer);

    SdpAnswer(const std::string& iceUFrag,
              const std::string& icePassword,
              const ExtensionMap& extensionMap,
              const std::vector<Host>& hostList,
              const std::shared_ptr<Track>& videoTrack,
              const std::shared_ptr<Track>& audioTrack);
    ~SdpAnswer();

    std::string getIceUFrag() const;
    std::string getIcePassword() const;
    ExtensionMap getExtensionMap() const;
    std::vector<Host> getHostList() const;
    std::shared_ptr<Track> getVideoTrack() const;
    std::shared_ptr<Track> getAudioTrack() const;

private:
    const std::string mIceUFrag;
    const std::string mIcePassword;
    const ExtensionMap mExtensionMap;
    const std::vector<Host> mHostList;
    const std::shared_ptr<Track> mVideoTrack;
    const std::shared_ptr<Track> mAudioTrack;
};

}
