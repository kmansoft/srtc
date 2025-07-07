#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/error.h"
#include "srtc/extension_map.h"
#include "srtc/srtc.h"
#include "srtc/x509_hash.h"

#include <memory>
#include <string>
#include <vector>

namespace srtc
{

class SdpOffer;
class Track;
class PeerConnection;
class TrackSelector;
class SdpAnswerParser;

class SdpAnswer
{
private:
    friend PeerConnection;
	friend SdpAnswerParser;

    static std::pair<std::shared_ptr<SdpAnswer>, Error> parse(Direction direction,
															  const std::shared_ptr<SdpOffer>& offer,
                                                              const std::string& answer,
                                                              const std::shared_ptr<TrackSelector>& selector);

public:
    ~SdpAnswer();

	[[nodiscard]] Direction getDirection() const;
    [[nodiscard]] std::string getIceUFrag() const;
    [[nodiscard]] std::string getIcePassword() const;
    [[nodiscard]] std::vector<Host> getHostList() const;
    [[nodiscard]] bool hasVideoMedia() const;
    [[nodiscard]] bool isVideoSimulcast() const;
    [[nodiscard]] std::shared_ptr<Track> getVideoSingleTrack() const;
    [[nodiscard]] std::vector<std::shared_ptr<Track>> getVideoSimulcastTrackList() const;
    [[nodiscard]] bool hasAudioMedia() const;
    [[nodiscard]] std::shared_ptr<Track> getAudioTrack() const;
    [[nodiscard]] const ExtensionMap& getVideoExtensionMap() const;
    [[nodiscard]] const ExtensionMap& getAudioExtensionMap() const;
    [[nodiscard]] bool isSetupActive() const;
    [[nodiscard]] const X509Hash& getCertificateHash() const;

private:
	const Direction mDirection;
    const std::string mIceUFrag;
    const std::string mIcePassword;
    const std::vector<Host> mHostList;
    const std::shared_ptr<Track> mVideoSingleTrack;
    const std::vector<std::shared_ptr<Track>> mVideoSimulcastTrackList;
    const std::shared_ptr<Track> mAudioTrack;
    const ExtensionMap mVideoExtensionMap;
    const ExtensionMap mAudioExtensionMap;
    const bool mIsSetupActive;
    const X509Hash mCertHash;

    SdpAnswer(Direction direction,
			  const std::string& iceUFrag,
              const std::string& icePassword,
              const std::vector<Host>& hostList,
              const std::shared_ptr<Track>& videoSingleTrack,
              const std::vector<std::shared_ptr<Track>>& videoSimulcastTrackList,
              const std::shared_ptr<Track>& audioTrack,
              const ExtensionMap& videoExtensionMap,
              const ExtensionMap& audioExtensionMap,
              bool isSetupActive,
              const X509Hash& certHash);
};

} // namespace srtc
