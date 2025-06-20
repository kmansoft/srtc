#include "srtc/sdp_answer.h"
#include "srtc/extension_map.h"
#include "srtc/sdp_offer.h"
#include "srtc/track.h"
#include "srtc/track_selector.h"
#include "srtc/util.h"
#include "srtc/x509_hash.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <cassert>
#include <cstring>

namespace
{

bool is_valid_payload_id(int payloadId)
{
	return payloadId > 0 && payloadId <= 127;
}

bool parse_line(const std::string& input,
				std::string& outTag,
				std::string& outKey,
				std::string& outValue,
				std::vector<std::string>& outProps)
{
	const auto posEq = input.find('=');
	if (posEq == std::string::npos) {
		return false;
	}
	outTag.assign(input.data(), input.data() + posEq);

	outKey.clear();
	outValue.clear();
	outProps.clear();

	const auto remaining = input.substr(posEq + 1);
	auto posSpace = remaining.find(' ');
	if (posSpace == std::string::npos) {
		posSpace = remaining.size();
	}

	for (size_t posColon = 0; posColon < posSpace; posColon += 1) {
		if (remaining[posColon] == ':') {
			outKey.assign(remaining.data(), remaining.data() + posColon);
			outValue.assign(remaining.data() + posColon + 1, remaining.data() + posSpace);
			break;
		}
	}

	if (outKey.empty()) {
		outKey.assign(remaining.data(), remaining.data() + posSpace);
	}

	auto curr = posSpace + 1;
	while (curr < remaining.size()) {
		const auto next = remaining.find(' ', curr);
		if (next == std::string::npos) {
			outProps.push_back(remaining.substr(curr));
			break;
		} else {
			outProps.push_back(remaining.substr(curr, next - curr));
			curr = next + 1;
		}
	}

	return true;
}

void parse_map(const std::string& line, std::unordered_map<std::string, std::string>& outMap)
{
	size_t curr = 0;
	while (true) {
		const auto next = line.find(';', curr);
		const auto prop = line.substr(curr, next - curr);

		const auto pos = prop.find('=');
		if (pos != std::string::npos) {
			outMap.emplace(prop.substr(0, pos), prop.substr(pos + 1));
		}

		if (next == std::string::npos) {
			break;
		}
		curr = next + 1;
	}
}

int parse_int(const std::string& s, int radix = 10)
{
	char* endptr = nullptr;
	long l = std::strtol(s.c_str(), &endptr, radix);
	if (endptr == s.c_str() + s.size()) {
		return static_cast<int>(l);
	}
	return -1;
}

srtc::Codec parse_codec(const std::string& s)
{
	if (s == "H264") {
		return srtc::Codec::H264;
	} else if (s == "opus") {
		return srtc::Codec::Opus;
	} else if (s == "rtx") {
		return srtc::Codec::Rtx;
	} else {
		return srtc::Codec::None;
	}
}

std::vector<std::string> split_list(const std::string& line)
{
	std::vector<std::string> list;

	size_t curr = 0;
	while (true) {
		const auto next = line.find(';', curr);
		const auto item = line.substr(curr, next - curr);

		list.push_back(item);

		if (next == std::string::npos) {
			break;
		}
		curr = next + 1;
	}

	return list;
}

struct ParsePayloadState {
	int payloadId = { -1 };
	srtc::Codec codec = { srtc::Codec::None };
	std::shared_ptr<srtc::Track::CodecOptions> codecOptions;
	uint32_t clockRate = { 0 };
	uint8_t rtxPayloadId = { 0 };
	bool hasNack = { false };
	bool hasPli = { false };
};

struct ParseMediaState {
	int id = { -1 };
	srtc::MediaType mediaType = { srtc::MediaType::None };
	srtc::ExtensionMap extensionMap;
	bool isSetupActive = { false };
	std::string mediaId;
	std::vector<std::string> ridList;
	std::vector<srtc::Track::SimulcastLayer> layerList;

	size_t payloadStateSize = { 0u };
	std::unique_ptr<ParsePayloadState[]> payloadStateList;

	void addSimulcastLayer(const std::vector<srtc::SimulcastLayer>& offerLayerList, const std::string& ridName);

	void setPayloadList(const std::vector<uint8_t>& list);

	[[nodiscard]] ParsePayloadState* getPayloadState(uint8_t payloadId) const;

	[[nodiscard]] std::shared_ptr<srtc::Track> selectTrack(uint32_t ssrc,
														   uint32_t rtxSsrc,
														   const std::shared_ptr<srtc::TrackSelector>& selector) const;

	[[nodiscard]] std::vector<std::shared_ptr<srtc::Track>> makeSimulcastTrackList(
		const std::shared_ptr<srtc::SdpOffer>& offer, const std::shared_ptr<srtc::Track>& singleTrack) const;
};

void ParseMediaState::addSimulcastLayer(const std::vector<srtc::SimulcastLayer>& offerLayerList,
										const std::string& ridName)
{
	for (size_t i = 0; i < offerLayerList.size(); i += 1) {
		const auto& layer = offerLayerList[i];
		if (layer.name == ridName) {
			layerList.push_back({
				{
					ridName,
					layer.width,
					layer.height,
					layer.frames_per_second,
					layer.kilobits_per_second,
				},
				static_cast<uint16_t>(i),
			});
			break;
		}
	}
}

void ParseMediaState::setPayloadList(const std::vector<uint8_t>& list)
{
	payloadStateSize = list.size();
	payloadStateList = std::make_unique<ParsePayloadState[]>(payloadStateSize);
	for (size_t i = 0u; i < payloadStateSize; i += 1) {
		payloadStateList[i].payloadId = list[i];
	}
}

ParsePayloadState* ParseMediaState::getPayloadState(uint8_t payloadId) const
{
	for (size_t i = 0u; i < payloadStateSize; i += 1) {
		if (payloadStateList[i].payloadId == payloadId) {
			return &payloadStateList[i];
		}
	}
	return nullptr;
}

std::shared_ptr<srtc::Track> ParseMediaState::selectTrack(uint32_t ssrc,
														  uint32_t rtxSsrc,
														  const std::shared_ptr<srtc::TrackSelector>& selector) const
{
	if (id < 0) {
		return nullptr;
	}

	std::vector<std::shared_ptr<srtc::Track>> list;
	for (size_t i = 0u; i < payloadStateSize; i += 1) {
		const auto& payloadState = payloadStateList[i];
		if (payloadState.payloadId > 0 && payloadState.codec != srtc::Codec::None &&
			payloadState.codec != srtc::Codec::Rtx) {
			const auto track = std::make_shared<srtc::Track>(id,
															 mediaType,
															 mediaId,
															 layerList.empty() ? ssrc : 0,
															 payloadState.payloadId,
															 layerList.empty() ? rtxSsrc : 0,
															 payloadState.rtxPayloadId,
															 payloadState.codec,
															 payloadState.codecOptions,
															 nullptr,
															 payloadState.clockRate,
															 payloadState.hasNack,
															 payloadState.hasPli);
			list.push_back(track);
		}
	}

	if (list.empty()) {
		return nullptr;
	}

	const auto selected = selector == nullptr ? list[0] : selector->selectTrack(mediaType, list);
	assert(selected != nullptr);
	return selected;
}

std::vector<std::shared_ptr<srtc::Track>> ParseMediaState::makeSimulcastTrackList(
	const std::shared_ptr<srtc::SdpOffer>& offer, const std::shared_ptr<srtc::Track>& singleTrack) const
{
	std::vector<std::shared_ptr<srtc::Track>> result;

	for (const auto& layer : layerList) {
		const auto ssrc = offer->getVideoSimulastSSRC(layer.name);
		const auto track = std::make_shared<srtc::Track>(id,
														 mediaType,
														 mediaId,
														 ssrc.first,
														 singleTrack->getPayloadId(),
														 ssrc.second,
														 singleTrack->getRtxPayloadId(),
														 singleTrack->getCodec(),
														 singleTrack->getCodecOptions(),
														 std::make_shared<srtc::Track::SimulcastLayer>(layer),
														 singleTrack->getClockRate(),
														 singleTrack->hasNack(),
														 singleTrack->hasPli());
		result.push_back(track);
	}

	return result;
}

} // namespace

namespace srtc
{

std::pair<std::shared_ptr<SdpAnswer>, Error> SdpAnswer::parse(const std::shared_ptr<SdpOffer>& offer,
															  const std::string& answer,
															  const std::shared_ptr<TrackSelector>& selector)
{
	std::stringstream ss(answer);

	std::string iceUFrag, icePassword;

	bool isRtcpMux = false;

	ParseMediaState mediaStateVideo, mediaStateAudio;
	mediaStateVideo.mediaType = MediaType::Video;
	mediaStateAudio.mediaType = MediaType::Audio;

	ParseMediaState* mediaStateCurr = nullptr;

	std::vector<Host> hostList;

	std::string certHashAlg;
	ByteBuffer certHashBin;
	std::string certHashHex;

	while (!ss.eof()) {
		std::string line;
		std::getline(ss, line);

		if (const auto sz = line.size()) {
			if (line[sz - 1] == '\r') {
				line.erase(sz - 1, sz);
			}
		}

		if (!line.empty()) {
			std::string tag, key, value;
			std::vector<std::string> props;

			parse_line(line, tag, key, value, props);

			if (tag == "a") {
				if (key == "rtcp-mux") {
					isRtcpMux = true;
				} else if (key == "ice-ufrag") {
					iceUFrag = value;
				} else if (key == "ice-pwd") {
					icePassword = value;
				} else if (key == "setup") {
					if (mediaStateCurr) {
						mediaStateCurr->isSetupActive = value == "active";
					}
				} else if (key == "fingerprint") {
					if (props.size() == 1) {
						certHashAlg = value;
						if (certHashAlg == "sha-256") {
							// TODO - for now
							certHashBin = hex_to_bin(props[0]);
							certHashHex = props[0];
						}
					}
				} else if (key == "extmap") {
					const auto id = parse_int(value);
					if (id >= 0 && id <= 255) {
						if (props.size() == 1 && mediaStateCurr) {
							mediaStateCurr->extensionMap.add(static_cast<uint8_t>(id), props[0]);
						}
					}
				} else if (key == "mid") {
					// a=mid:0
					if (mediaStateCurr && !value.empty()) {
						mediaStateCurr->mediaId = value;
					}
				} else if (key == "rtpmap") {
					// a=rtpmap:100 H264/90000
					// a=rtpmap:99 opus/48000/2
					if (const auto payloadId = parse_int(value); is_valid_payload_id(payloadId)) {
						if (props.size() == 1 && mediaStateCurr) {
							const auto posSlash = props[0].find('/');
							if (posSlash != std::string::npos) {
								const auto codecString = props[0].substr(0, posSlash);
								if (const auto codec = parse_codec(codecString); codec != Codec::None) {
									auto clockRateString = props[0].substr(posSlash + 1);
									const auto posSlash2 = clockRateString.find('/');
									if (posSlash2 != std::string::npos) {
										clockRateString.resize(posSlash2);
									}
									if (const auto clockRate = parse_int(clockRateString); clockRate >= 10000) {
										const auto payloadState = mediaStateCurr->getPayloadState(payloadId);
										if (payloadState) {
											payloadState->codec = codec;
											payloadState->clockRate = clockRate;
											payloadState->payloadId = payloadId;
										}
									}
								}
							}
						}
					}
				} else if (key == "fmtp") {
					// a=fmtp:100 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f
					if (const auto payloadId = parse_int(value); is_valid_payload_id(payloadId) && mediaStateCurr) {
						if (props.size() == 1) {
							std::unordered_map<std::string, std::string> map;
							parse_map(props[0], map);

							if (const auto iter1 = map.find("apt"); iter1 != map.end()) {
								const auto payloadState = mediaStateCurr->getPayloadState(payloadId);
								if (payloadState && payloadState->codec == Codec::Rtx) {
									const auto referencedPayloadId = parse_int(iter1->second);
									if (is_valid_payload_id(referencedPayloadId)) {
										const auto referencedPayloadState =
											mediaStateCurr->getPayloadState(referencedPayloadId);
										if (referencedPayloadState) {
											referencedPayloadState->rtxPayloadId = payloadId;
										}
									}
								}
							}

							const auto payloadState = mediaStateCurr->getPayloadState(payloadId);
							if (payloadState) {
								int profileLevelId = 0;
								int minptime = 0;
								bool stereo = false;

								if (mediaStateCurr == &mediaStateVideo) {
									if (const auto iter2 = map.find("profile-level-id"); iter2 != map.end()) {
										profileLevelId = parse_int(iter2->second, 16);
									}
								} else if (mediaStateCurr == &mediaStateAudio) {
									if (const auto iter2 = map.find("minptime"); iter2 != map.end()) {
										minptime = parse_int(iter2->second);
									}
									if (const auto iter3 = map.find("stereo"); iter3 != map.end()) {
										stereo = parse_int(iter3->second) != 0;
									}
								}

								if (profileLevelId != 0 || minptime != 0 || stereo) {
									payloadState->codecOptions =
										std::make_shared<srtc::Track::CodecOptions>(profileLevelId, minptime, stereo);
								}
							}
						}
					}
				} else if (key == "rtcp-fb") {
					// a=rtcp-fb:98 nack pli
					if (const auto payloadId = parse_int(value); is_valid_payload_id(payloadId) && mediaStateCurr) {
						const auto payloadState = mediaStateCurr->getPayloadState(payloadId);
						if (payloadState) {
							for (size_t i = 0u; i < props.size(); i += 1) {
								const auto& token = props[i];
								if (token == "nack") {
									payloadState->hasNack = true;
								} else if (token == "pli") {
									payloadState->hasPli = true;
								}
							}
						}
					}
				} else if (key == "rid") {
					// a=rid:low
					if (mediaStateCurr && mediaStateCurr == &mediaStateVideo) {
						if (props.size() == 1 && props[0] == "recv") {
							mediaStateCurr->ridList.push_back(value);
						}
					}
				} else if (key == "simulcast") {
					// a=simulcast recv low;mid;hi
					if (mediaStateCurr && mediaStateCurr == &mediaStateVideo) {
						if (value == "recv" && props.size() == 1) {
							const auto offerLayerList = offer->getVideoSimulcastLayerList();
							if (offerLayerList.has_value()) {
								const auto ridList = split_list(props[0]);
								for (const auto& ridName : ridList) {
									mediaStateCurr->addSimulcastLayer(offerLayerList.value(), ridName);
								}
							}
						}
					}
				} else if (key == "candidate") {
					// a=candidate:182981660 1 udp 2130706431 99.181.107.72 443 typ host
					if (props.size() >= 7) {
						if (props[0] == "1" && props[1] == "udp" && props[5] == "typ" && props[6] == "host") {
							if (const auto port = parse_int(props[4]); port > 0 && port < 63536) {
								Host host{};

								const auto& addrStr = props[3];
								if (addrStr.find('.') != std::string::npos) {
									if (inet_pton(AF_INET, addrStr.c_str(), &host.addr.sin_ipv4.sin_addr) > 0) {
										if (std::find_if(hostList.begin(), hostList.end(), [host](const Host& it) {
												return host.addr == it.addr;
											}) == hostList.end()) {
											host.addr.ss.ss_family = AF_INET;
											host.addr.sin_ipv4.sin_port = htons(port);
											hostList.push_back(host);
										}
									}
								} else if (addrStr.find(':') != std::string::npos) {
									if (inet_pton(AF_INET6, addrStr.c_str(), &host.addr.sin_ipv6.sin6_addr) > 0) {
										if (std::find_if(hostList.begin(), hostList.end(), [host](const Host& it) {
												return host.addr == it.addr;
											}) == hostList.end()) {
											host.addr.ss.ss_family = AF_INET6;
											host.addr.sin_ipv6.sin6_port = htons(port);
											hostList.push_back(host);
										}
									}
								}
							}
						}
					}
				}
			} else if (tag == "m") {
				// "m=video 9 UDP/TLS/RTP/SAVPF 96 97 98"
				if (props.size() < 2 || props[1] != "UDP/TLS/RTP/SAVPF") {
					return { {}, { Error::Code::InvalidData, "Only SAVPF over DTLS is supported" } };
				}

				if (key == "video") {
					mediaStateCurr = &mediaStateVideo;
				} else if (key == "audio") {
					mediaStateCurr = &mediaStateAudio;
				} else {
					mediaStateCurr = nullptr;
				}

				if (mediaStateCurr) {
					if (const auto id = parse_int(props[0]); id >= 0) {
						mediaStateCurr->id = id;
					}

					std::vector<uint8_t> payloadIdList;
					for (size_t i = 2u; i < props.size(); i += 1) {
						if (const auto payloadId = parse_int(props[i]); is_valid_payload_id(payloadId)) {
							payloadIdList.push_back(payloadId);
						}
					}

					mediaStateCurr->setPayloadList(payloadIdList);
				}
			}
		}
	}

	if (!isRtcpMux) {
		return { {}, { Error::Code::InvalidData, "The rtcp-mux extension is required" } };
	}
	if (hostList.empty()) {
		return { {}, { Error::Code::InvalidData, "No hosts to connect to" } };
	}

	if (mediaStateVideo.id < 0 && mediaStateAudio.id < 0) {
		return { {}, { Error::Code::InvalidData, "No media tracks" } };
	}

	auto videoSingleTrack = mediaStateVideo.selectTrack(offer->getVideoSSRC(), offer->getRtxVideoSSRC(), selector);
	const auto audioTrack = mediaStateAudio.selectTrack(offer->getAudioSSRC(), offer->getRtxAudioSSRC(), selector);

	if (!videoSingleTrack && !audioTrack) {
		return { nullptr, { Error::Code::InvalidData, "No media tracks" } };
	}

	std::vector<std::shared_ptr<Track>> videoSimulcastTrackList;
	if (!mediaStateVideo.layerList.empty()) {
		videoSimulcastTrackList = mediaStateVideo.makeSimulcastTrackList(offer, videoSingleTrack);
		videoSingleTrack.reset();
	}

	std::shared_ptr<SdpAnswer> sdpAnswer(new SdpAnswer(iceUFrag,
													   icePassword,
													   hostList,
													   videoSingleTrack,
													   videoSimulcastTrackList,
													   audioTrack,
													   mediaStateVideo.extensionMap,
													   mediaStateAudio.extensionMap,
													   mediaStateVideo.isSetupActive || mediaStateAudio.isSetupActive,
													   { certHashAlg, certHashBin, certHashHex }));

	return { sdpAnswer, Error::OK };
}

SdpAnswer::SdpAnswer(const std::string& iceUFrag,
					 const std::string& icePassword,
					 const std::vector<Host>& hostList,
					 const std::shared_ptr<Track>& videoSingleTrack,
					 const std::vector<std::shared_ptr<Track>>& videoSimulcastTrackList,
					 const std::shared_ptr<Track>& audioTrack,
					 const ExtensionMap& videoExtensionMap,
					 const ExtensionMap& audioExtensionMap,
					 bool isSetupActive,
					 const X509Hash& certHash)
	: mIceUFrag(iceUFrag)
	, mIcePassword(icePassword)
	, mHostList(hostList)
	, mVideoSingleTrack(videoSingleTrack)
	, mVideoSimulcastTrackList(videoSimulcastTrackList)
	, mAudioTrack(audioTrack)
	, mVideoExtensionMap(videoExtensionMap)
	, mAudioExtensionMap(audioExtensionMap)
	, mIsSetupActive(isSetupActive)
	, mCertHash(certHash)
{
}

SdpAnswer::~SdpAnswer() = default;

std::string SdpAnswer::getIceUFrag() const
{
	return mIceUFrag;
}

std::string SdpAnswer::getIcePassword() const
{
	return mIcePassword;
}

const ExtensionMap& SdpAnswer::getVideoExtensionMap() const
{
	return mVideoExtensionMap;
}

const ExtensionMap& SdpAnswer::getAudioExtensionMap() const
{
	return mAudioExtensionMap;
}

std::vector<Host> SdpAnswer::getHostList() const
{
	return mHostList;
}

bool SdpAnswer::hasVideoMedia() const
{
	return mVideoSingleTrack != nullptr || !mVideoSimulcastTrackList.empty();
}

bool SdpAnswer::isVideoSimulcast() const
{
	return mVideoSingleTrack == nullptr && !mVideoSimulcastTrackList.empty();
}

std::shared_ptr<Track> SdpAnswer::getVideoSingleTrack() const
{
	return mVideoSingleTrack;
}

std::vector<std::shared_ptr<Track>> SdpAnswer::getVideoSimulcastTrackList() const
{
	return mVideoSimulcastTrackList;
}

bool SdpAnswer::hasAudioMedia() const
{
	return mAudioTrack != nullptr;
}

std::shared_ptr<Track> SdpAnswer::getAudioTrack() const
{
	return mAudioTrack;
}

bool SdpAnswer::isSetupActive() const
{
	return mIsSetupActive;
}

const X509Hash& SdpAnswer::getCertificateHash() const
{
	return mCertHash;
}

} // namespace srtc
