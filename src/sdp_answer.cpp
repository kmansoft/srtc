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

std::optional<int> parse_int(const std::string& s, int radix = 10)
{
	char* endptr = nullptr;
	long l = std::strtol(s.c_str(), &endptr, radix);
	if (endptr == s.c_str() + s.size()) {
		return static_cast<int>(l);
	}
	return {};
}

std::optional<srtc::Codec> parse_codec(const std::string& s)
{
	if (s == "H264") {
		return srtc::Codec::H264;
	} else if (s == "opus") {
		return srtc::Codec::Opus;
	} else if (s == "rtx") {
		return srtc::Codec::Rtx;
	} else {
		return {};
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
	uint8_t payloadId = { 0 };
	std::optional<srtc::Codec> codec;
	std::shared_ptr<srtc::Track::CodecOptions> codecOptions;
	uint32_t clockRate = { 0 };
	uint8_t rtxPayloadId = { 0 };
	bool hasNack = { false };
	bool hasPli = { false };
};

struct ParseMediaState {
	std::optional<int> id;
	std::optional<srtc::MediaType> mediaType;
	srtc::ExtensionMap extensionMap;
	bool isSetupActive = { false };
	std::string mediaId;
	std::vector<std::string> ridList;
	std::vector<srtc::Track::SimulcastLayer> layerList;

	size_t payloadStateSize = { 0u };
	std::unique_ptr<ParsePayloadState[]> payloadStateList;

	uint32_t ssrc = { 0u };
	uint32_t rtxSsrc = { 0u };
	std::vector<uint32_t> ssrcList;

	void addSimulcastLayer(const std::vector<srtc::SimulcastLayer>& offerLayerList, const std::string& ridName);

	void setPayloadList(const std::vector<uint8_t>& list);

	[[nodiscard]] ParsePayloadState* getPayloadState(uint8_t payloadId) const;

	[[nodiscard]] std::shared_ptr<srtc::Track> selectTrack(srtc::Direction direction,
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

std::shared_ptr<srtc::Track> ParseMediaState::selectTrack(srtc::Direction direction,
														  const std::shared_ptr<srtc::TrackSelector>& selector) const
{
	if (!id.has_value()) {
		return nullptr;
	}
	if (!mediaType.has_value()) {
		return nullptr;
	}

	auto ssrcMedia = ssrc;

	if (direction == srtc::Direction::Subscribe) {
		if (ssrcMedia == 0 && !ssrcList.empty()) {
			ssrcMedia = ssrcList.front();
		}
		if (ssrcMedia <= 0) {
			return nullptr;
		}
	}

	std::vector<std::shared_ptr<srtc::Track>> list;
	for (size_t i = 0u; i < payloadStateSize; i += 1) {
		const auto& payloadState = payloadStateList[i];
		if (payloadState.payloadId > 0 && payloadState.codec.has_value() && payloadState.codec != srtc::Codec::Rtx) {
			const auto trackSsrc = layerList.empty() ? ssrcMedia : 0;
			const auto trackRtxSsrc = layerList.empty() && payloadState.rtxPayloadId != 0 ? rtxSsrc : 0;

			const auto track = std::make_shared<srtc::Track>(id.value(),
															 direction,
															 mediaType.value(),
															 mediaId,
															 trackSsrc,
															 payloadState.payloadId,
															 trackRtxSsrc,
															 payloadState.rtxPayloadId,
															 payloadState.codec.value(),
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

	const auto selected = selector == nullptr ? list[0] : selector->selectTrack(mediaType.value(), list);
	assert(selected != nullptr);
	return selected;
}

std::vector<std::shared_ptr<srtc::Track>> ParseMediaState::makeSimulcastTrackList(
	const std::shared_ptr<srtc::SdpOffer>& offer, const std::shared_ptr<srtc::Track>& singleTrack) const
{
	std::vector<std::shared_ptr<srtc::Track>> result;

	for (const auto& layer : layerList) {
		const auto layerSsrc = offer->getVideoSimulastSSRC(layer.name);
		const auto track = std::make_shared<srtc::Track>(id.value(),
														 offer->getDirection(),
														 mediaType.value(),
														 mediaId,
														 layerSsrc.first,
														 singleTrack->getPayloadId(),
														 layerSsrc.second,
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
class SdpAnswerParser
{
public:
	SdpAnswerParser(Direction direction, const std::shared_ptr<SdpOffer>& offer);

	std::pair<std::shared_ptr<SdpAnswer>, Error> parse(const std::string& answer,
													   const std::shared_ptr<TrackSelector>& selector);

private:
	Error parseLine(const std::string& line);

	Error parseLine_m(const std::string& tag,
					  const std::string& key,
					  const std::string& value,
					  const std::vector<std::string>& props);
	Error parseLine_a(const std::string& tag,
					  const std::string& key,
					  const std::string& value,
					  const std::vector<std::string>& props);

	Direction direction;
	std::shared_ptr<SdpOffer> offer;

	std::string iceUFrag, icePassword;

	bool isRtcpMux = false;

	ParseMediaState mediaStateVideo, mediaStateAudio;
	ParseMediaState* mediaStateCurr = nullptr;

	std::vector<Host> hostList;

	std::string certHashAlg;
	ByteBuffer certHashBin;
	std::string certHashHex;
};

SdpAnswerParser::SdpAnswerParser(Direction direction, const std::shared_ptr<SdpOffer>& offer)
	: direction(direction)
	, offer(offer)
{
	mediaStateVideo.mediaType = MediaType::Video;
	mediaStateAudio.mediaType = MediaType::Audio;

	if (direction == Direction::Publish) {
		mediaStateVideo.ssrc = offer->getVideoSSRC();
		mediaStateVideo.rtxSsrc = offer->getRtxVideoSSRC();

		mediaStateAudio.ssrc = offer->getAudioSSRC();
		mediaStateAudio.rtxSsrc = offer->getRtxAudioSSRC();
	}
}

std::pair<std::shared_ptr<SdpAnswer>, Error> SdpAnswerParser::parse(const std::string& answer,
																	const std::shared_ptr<TrackSelector>& selector)
{
	std::stringstream ss(answer);

	while (!ss.eof()) {
		std::string line;
		std::getline(ss, line);

		// Remove \r chars
		line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

		// Process
		if (!line.empty()) {
			if (const auto error = parseLine(line); error.isError()) {
				return { nullptr, error };
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

	auto videoSingleTrack = mediaStateVideo.selectTrack(direction, selector);
	const auto audioTrack = mediaStateAudio.selectTrack(direction, selector);

	if (!videoSingleTrack && !audioTrack) {
		return { nullptr, { Error::Code::InvalidData, "No media tracks" } };
	}

	std::vector<std::shared_ptr<Track>> videoSimulcastTrackList;
	if (!mediaStateVideo.layerList.empty()) {
		videoSimulcastTrackList = mediaStateVideo.makeSimulcastTrackList(offer, videoSingleTrack);
		videoSingleTrack.reset();
	}

	std::shared_ptr<SdpAnswer> sdpAnswer(new SdpAnswer(direction,
													   iceUFrag,
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

Error SdpAnswerParser::parseLine(const std::string& line)
{
	std::string tag, key, value;
	std::vector<std::string> props;

	if (!parse_line(line, tag, key, value, props)) {
		return { Error::Code::InvalidData, "Invalid line in the SDP answer" };
	}

	if (tag == "a") {
		if (const auto error = parseLine_a(tag, key, value, props); error.isError()) {
			return error;
		}
	} else if (tag == "m") {
		if (const auto error = parseLine_m(tag, key, value, props); error.isError()) {
			return error;
		}
	}

	return Error::OK;
}

Error SdpAnswerParser::parseLine_m(const std::string& tag,
								   const std::string& key,
								   const std::string& value,
								   const std::vector<std::string>& props)
{
	// "m=video 9 UDP/TLS/RTP/SAVPF 96 97 98"
	if (props.size() < 2 || props[1] != "UDP/TLS/RTP/SAVPF") {
		return { Error::Code::InvalidData, "Only SAVPF over DTLS is supported" };
	}

	if (key == "video") {
		mediaStateCurr = &mediaStateVideo;
	} else if (key == "audio") {
		mediaStateCurr = &mediaStateAudio;
	} else {
		mediaStateCurr = nullptr;
	}

	if (mediaStateCurr) {
		if (const auto id = parse_int(props[0]); id.has_value()) {
			mediaStateCurr->id = id.value();
		}

		std::vector<uint8_t> payloadIdList;
		for (size_t i = 2u; i < props.size(); i += 1) {
			if (const auto payloadId = parse_int(props[i]);
				payloadId.has_value() && is_valid_payload_id(payloadId.value())) {
				payloadIdList.push_back(payloadId.value());
			}
		}

		mediaStateCurr->setPayloadList(payloadIdList);
	}

	return Error::OK;
}

Error SdpAnswerParser::parseLine_a(const std::string& tag,
								   const std::string& key,
								   const std::string& value,
								   const std::vector<std::string>& props)
{
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
		if (id.has_value() && id >= 0 && id <= 255) {
			if (props.size() == 1 && mediaStateCurr) {
				mediaStateCurr->extensionMap.add(static_cast<uint8_t>(id.value()), props[0]);
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
		if (const auto payloadId = parse_int(value); payloadId.has_value() && is_valid_payload_id(payloadId.value())) {
			if (props.size() == 1 && mediaStateCurr) {
				const auto posSlash = props[0].find('/');
				if (posSlash != std::string::npos) {
					const auto codecString = props[0].substr(0, posSlash);
					if (const auto codec = parse_codec(codecString); codec.has_value()) {
						auto clockRateString = props[0].substr(posSlash + 1);
						const auto posSlash2 = clockRateString.find('/');
						if (posSlash2 != std::string::npos) {
							clockRateString.resize(posSlash2);
						}
						if (const auto clockRate = parse_int(clockRateString);
							clockRate.has_value() && clockRate >= 10000) {
							const auto payloadState = mediaStateCurr->getPayloadState(payloadId.value());
							if (payloadState) {
								payloadState->codec = codec;
								payloadState->clockRate = clockRate.value();
								payloadState->payloadId = payloadId.value();
							}
						}
					}
				}
			}
		}
	} else if (key == "fmtp") {
		// a=fmtp:100 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f
		if (const auto payloadId = parse_int(value);
			payloadId.has_value() && is_valid_payload_id(payloadId.value()) && mediaStateCurr) {
			if (props.size() == 1) {
				std::unordered_map<std::string, std::string> map;
				parse_map(props[0], map);

				if (const auto iter1 = map.find("apt"); iter1 != map.end()) {
					const auto payloadState = mediaStateCurr->getPayloadState(payloadId.value());
					if (payloadState && payloadState->codec == Codec::Rtx) {
						if (const auto referencedPayloadId = parse_int(iter1->second);
							referencedPayloadId.has_value() && is_valid_payload_id(referencedPayloadId.value())) {
							const auto referencedPayloadState =
								mediaStateCurr->getPayloadState(referencedPayloadId.value());
							if (referencedPayloadState) {
								referencedPayloadState->rtxPayloadId = payloadId.value();
							}
						}
					}
				}

				const auto payloadState = mediaStateCurr->getPayloadState(payloadId.value());
				if (payloadState) {
					int profileLevelId = 0;
					int minptime = 0;
					bool stereo = false;

					if (mediaStateCurr == &mediaStateVideo) {
						if (const auto iter2 = map.find("profile-level-id"); iter2 != map.end()) {
							if (const auto parsed = parse_int(iter2->second, 16); parsed.has_value()) {
								profileLevelId = parsed.value();
							}
						}
					} else if (mediaStateCurr == &mediaStateAudio) {
						if (const auto iter2 = map.find("minptime"); iter2 != map.end()) {
							if (const auto parsed = parse_int(iter2->second); parsed.has_value()) {
								minptime = parsed.value();
							}
						}
						if (const auto iter3 = map.find("stereo"); iter3 != map.end()) {
							if (const auto parsed = parse_int(iter3->second); parsed.has_value()) {
								stereo = parsed.value() != 0;
							}
						}
					}

					if (profileLevelId != 0 || minptime != 0 || stereo) {
						payloadState->codecOptions =
							std::make_shared<Track::CodecOptions>(profileLevelId, minptime, stereo);
					}
				}
			}
		}
	} else if (key == "rtcp-fb") {
		// a=rtcp-fb:98 nack pli
		if (const auto payloadId = parse_int(value);
			payloadId.has_value() && is_valid_payload_id(payloadId.value()) && mediaStateCurr) {
			const auto payloadState = mediaStateCurr->getPayloadState(payloadId.value());
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
				if (const auto port = parse_int(props[4]); port.has_value() && port > 0 && port < 63536) {
					Host host{};

					const auto& addrStr = props[3];
					if (addrStr.find('.') != std::string::npos) {
						if (inet_pton(AF_INET, addrStr.c_str(), &host.addr.sin_ipv4.sin_addr) > 0) {
							if (std::find_if(hostList.begin(), hostList.end(), [host](const Host& it) {
									return host.addr == it.addr;
								}) == hostList.end()) {
								host.addr.ss.ss_family = AF_INET;
								host.addr.sin_ipv4.sin_port = htons(port.value());
								hostList.push_back(host);
							}
						}
					} else if (addrStr.find(':') != std::string::npos) {
						if (inet_pton(AF_INET6, addrStr.c_str(), &host.addr.sin_ipv6.sin6_addr) > 0) {
							if (std::find_if(hostList.begin(), hostList.end(), [host](const Host& it) {
									return host.addr == it.addr;
								}) == hostList.end()) {
								host.addr.ss.ss_family = AF_INET6;
								host.addr.sin_ipv6.sin6_port = htons(port.value());
								hostList.push_back(host);
							}
						}
					}
				}
			}
		}
	} else if (key == "ssrc-group") {
		// RTX
		if (value == "FID" && props.size() == 2) {
			const auto ssrcMedia = parse_int(props[0]);
			const auto ssrcRtx = parse_int(props[1]);
			if (mediaStateCurr && ssrcMedia.has_value() && ssrcRtx.has_value()) {
				mediaStateCurr->ssrc = ssrcMedia.value();
				mediaStateCurr->rtxSsrc = ssrcRtx.value();
			}
		}
	} else if (key == "ssrc") {
		const auto ssrcMedia = parse_int(value);
		if (mediaStateCurr && ssrcMedia.has_value()) {
			if (std::find_if(
					mediaStateCurr->ssrcList.begin(), mediaStateCurr->ssrcList.end(), [ssrcMedia](uint32_t ssrc) {
						return ssrc == ssrcMedia;
					}) == mediaStateCurr->ssrcList.end()) {
				mediaStateCurr->ssrcList.push_back(ssrcMedia.value());
			}
		}
	}

	return Error::OK;
}

std::pair<std::shared_ptr<SdpAnswer>, Error> SdpAnswer::parse(Direction direction,
															  const std::shared_ptr<SdpOffer>& offer,
															  const std::string& answer,
															  const std::shared_ptr<TrackSelector>& selector)
{
	SdpAnswerParser parser(direction, offer);
	return parser.parse(answer, selector);
}

SdpAnswer::SdpAnswer(Direction direction,
					 const std::string& iceUFrag,
					 const std::string& icePassword,
					 const std::vector<Host>& hostList,
					 const std::shared_ptr<Track>& videoSingleTrack,
					 const std::vector<std::shared_ptr<Track>>& videoSimulcastTrackList,
					 const std::shared_ptr<Track>& audioTrack,
					 const ExtensionMap& videoExtensionMap,
					 const ExtensionMap& audioExtensionMap,
					 bool isSetupActive,
					 const X509Hash& certHash)
	: mDirection(direction)
	, mIceUFrag(iceUFrag)
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

Direction SdpAnswer::getDirection() const
{
	return mDirection;
}

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
