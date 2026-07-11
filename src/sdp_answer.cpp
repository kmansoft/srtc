#include "srtc/sdp_answer.h"
#include "srtc/extension_map.h"
#include "srtc/media.h"
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

namespace
{

bool is_valid_payload_id(unsigned int payloadId)
{
    return payloadId >= 96u && payloadId <= 127u;
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

std::optional<uint32_t> parse_u32(const std::string& s, int radix = 10)
{
    char* endptr = nullptr;
    const auto l = std::strtoul(s.c_str(), &endptr, radix);
    if (endptr == s.c_str() + s.size()) {
        return static_cast<uint32_t>(l);
    }
    return {};
}

std::optional<srtc::Codec> parse_codec(const std::string& s)
{
    if (s == "VP8") {
        return srtc::Codec::VP8;
    }
    if (s == "VP9") {
        return srtc::Codec::VP9;
    }
    if (s == "H264") {
        return srtc::Codec::H264;
    }
    if (s == "H265") {
        return srtc::Codec::H265;
    }
    if (s == "AV1") {
        return srtc::Codec::AV1;
    }
    if (s == "opus") {
        return srtc::Codec::Opus;
    }
    if (s == "rtx") {
        return srtc::Codec::Rtx;
    }

    return {};
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
    std::optional<srtc::MediaType> mediaType;
    std::string mediaId;
    srtc::ExtensionMap extensionMap;
    std::vector<std::string> ridList;
    std::vector<srtc::Track::SimulcastLayer> layerList;

    size_t payloadStateSize = { 0u };
    std::unique_ptr<ParsePayloadState[]> payloadStateList;

    uint32_t ssrc = { 0u };
    uint32_t rtxSsrc = { 0u };
    std::vector<uint32_t> ssrcList;

    void clear();

    void addSimulcastLayer(const std::vector<srtc::SimulcastLayer>& offerLayerList, const std::string& ridName);

    void setPayloadList(const std::vector<uint8_t>& list);

    [[nodiscard]] ParsePayloadState* getPayloadState(uint8_t payloadId) const;

    [[nodiscard]] std::shared_ptr<srtc::Media> createMedia() const;

    [[nodiscard]] std::shared_ptr<srtc::Track> selectTrack(srtc::Direction direction,
                                                           const std::shared_ptr<srtc::Media>& media,
                                                           const std::shared_ptr<srtc::TrackSelector>& selector) const;

    [[nodiscard]] std::vector<std::shared_ptr<srtc::Track>> makeSimulcastTrackList(
        const std::shared_ptr<srtc::SdpOffer>& offer, const std::shared_ptr<srtc::Track>& singleTrack) const;
};

void ParseMediaState::clear()
{
    mediaType.reset();
    mediaId.clear();
    extensionMap.clear();
    ridList.clear();
    layerList.clear();
    payloadStateSize = 0;
    payloadStateList.reset();
    ssrc = 0;
    rtxSsrc = 0;
    ssrcList.clear();
}

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
                static_cast<uint8_t>(i),
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

std::shared_ptr<srtc::Media> ParseMediaState::createMedia() const
{
    return std::make_shared<srtc::Media>(mediaId, mediaType.value(), extensionMap);
}

std::shared_ptr<srtc::Track> ParseMediaState::selectTrack(srtc::Direction direction,
                                                          const std::shared_ptr<srtc::Media>& media,
                                                          const std::shared_ptr<srtc::TrackSelector>& selector) const
{
    if (mediaId.empty()) {
        return {};
    }
    if (!mediaType.has_value()) {
        return {};
    }

    auto ssrcMedia = ssrc;

    if (direction == srtc::Direction::Subscribe) {
        if (ssrcMedia == 0 && !ssrcList.empty()) {
            ssrcMedia = ssrcList.front();
        }
        if (ssrcMedia <= 0) {
            return {};
        }
    }

    std::vector<std::shared_ptr<srtc::Track>> list;
    for (size_t i = 0u; i < payloadStateSize; i += 1) {
        const auto& payloadState = payloadStateList[i];
        if (payloadState.payloadId > 0 && payloadState.codec.has_value() && payloadState.codec != srtc::Codec::Rtx) {
            const auto trackSsrc = layerList.empty() ? ssrcMedia : 0;
            const auto trackRtxSsrc = layerList.empty() && payloadState.rtxPayloadId != 0 ? rtxSsrc : 0;

            const auto track = std::make_shared<srtc::Track>(media,
                                                             direction,
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
        return {};
    }

    const auto selected = selector == nullptr ? list[0] : selector->selectTrack(list);
    assert(selected != nullptr);
    return selected;
}

std::vector<std::shared_ptr<srtc::Track>> ParseMediaState::makeSimulcastTrackList(
    const std::shared_ptr<srtc::SdpOffer>& offer, const std::shared_ptr<srtc::Track>& singleTrack) const
{
    std::vector<std::shared_ptr<srtc::Track>> result;

    for (const auto& layer : layerList) {
        const auto layerSsrc = offer->getVideoSimulastSSRC(mediaId, layer.name);
        const auto track = std::make_shared<srtc::Track>(singleTrack->getMedia(),
                                                         singleTrack->getDirection(),
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
    SdpAnswerParser(const std::shared_ptr<SdpOffer>& offer, const std::shared_ptr<TrackSelector>& selector);

    std::pair<std::shared_ptr<SdpAnswer>, Error> parse(const std::string& answer);

private:
    Error parseLine(const std::string& line);

    Error parseLine_m(const std::string& key,
                      const std::string& value,
                      const std::vector<std::string>& props);
    Error parseLine_a(const std::string& key,
                      const std::string& value,
                      const std::vector<std::string>& props);

    Error flush_m();

    const std::shared_ptr<SdpOffer> offer;
    const Direction direction;
    const std::shared_ptr<TrackSelector> selector;

    std::string iceUFrag, icePassword;

    bool isRtcpMux = false;
    bool isSetupActive = false;

    bool isInMediaSection = false;
    bool hasMedia = false;
    ParseMediaState mediaState;

    bool isInApplicationSection = false;
    bool hasDataChannel = false;
    uint16_t sctpPort = 5000;
    uint32_t maxSctpMessageSize = 262144;

    std::vector<std::shared_ptr<Media>> mediaList;
    std::vector<std::shared_ptr<Track>> trackList;
    std::vector<Host> hostList;

    std::string certHashAlg;
    ByteBuffer certHashBin;
    std::string certHashHex;
};

SdpAnswerParser::SdpAnswerParser(const std::shared_ptr<SdpOffer>& offer, const std::shared_ptr<TrackSelector>& selector)
    : offer(offer)
    , direction(offer->getDirection())
    , selector(selector)
{
}

std::pair<std::shared_ptr<SdpAnswer>, Error> SdpAnswerParser::parse(const std::string& answer)
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

    if (const auto error = flush_m(); error.isError()) {
        return { {}, error };
    }

    if (hasMedia && !isRtcpMux) {
        return { {}, { Error::Code::InvalidData, "The rtcp-mux extension is required" } };
    }
    if (hostList.empty()) {
        return { {}, { Error::Code::InvalidData, "No hosts to connect to" } };
    }

    if (!hasMedia && !hasDataChannel) {
        return { {}, { Error::Code::InvalidData, "No media tracks or data channels" } };
    }

    std::shared_ptr<SdpAnswer> sdpAnswer(new SdpAnswer(direction,
                                                       iceUFrag,
                                                       icePassword,
                                                       hostList,
                                                       mediaList,
                                                       trackList,
                                                       isSetupActive,
                                                       { certHashAlg, certHashBin, certHashHex },
                                                       hasDataChannel,
                                                       sctpPort,
                                                       maxSctpMessageSize));

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
        if (const auto error = parseLine_a(key, value, props); error.isError()) {
            return error;
        }
    } else if (tag == "m") {
        if (const auto error = parseLine_m(key, value, props); error.isError()) {
            return error;
        }
    }

    return Error::OK;
}

Error SdpAnswerParser::parseLine_m(const std::string& key,
                                   [[maybe_unused]] const std::string& value,
                                   const std::vector<std::string>& props)
{
    if (const auto error = flush_m(); error.isError()) {
        return error;
    }

    mediaState.clear();

    isInApplicationSection = false;
    isInMediaSection = false;

    // "m=application 9 UDP/DTLS/SCTP webrtc-datachannel"
    if (key == "application") {
        if (props.size() >= 2 && props[1] == "UDP/DTLS/SCTP") {
            isInApplicationSection = true;
            hasDataChannel = true;
        }
        return Error::OK;
    }

    // "m=video 9 UDP/TLS/RTP/SAVPF 96 97 98"
    if (props.size() < 2 || props[1] != "UDP/TLS/RTP/SAVPF") {
        return { Error::Code::InvalidData, "Only SAVPF over DTLS is supported" };
    }

    if (key == "video" || key == "audio") {
        isInMediaSection = true;

        std::vector<uint8_t> payloadIdList;
        for (size_t i = 2u; i < props.size(); i += 1) {
            if (const auto payloadId = parse_u32(props[i]);
                payloadId.has_value() && is_valid_payload_id(payloadId.value())) {
                payloadIdList.push_back(static_cast<uint8_t>(payloadId.value()));
            }
        }

        mediaState.setPayloadList(payloadIdList);

        if (key == "video") {
            mediaState.mediaType = MediaType::Video;
        } else if (key == "audio") {
            mediaState.mediaType = MediaType::Audio;
        }
    }

    return Error::OK;
}

Error SdpAnswerParser::parseLine_a(const std::string& key,
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
        if (value == "active") {
            isSetupActive = true;
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
        const auto id = parse_u32(value);
        if (id.has_value() && id > 0u && id <= 255u) {
            if (props.size() == 1 && isInMediaSection) {
                mediaState.extensionMap.add(static_cast<uint8_t>(id.value()), props[0]);
            }
        }
    } else if (key == "mid") {
        // a=mid:0
        if (isInMediaSection && !value.empty()) {
            mediaState.mediaId = value;
        }
    } else if (key == "rtpmap") {
        // a=rtpmap:100 H264/90000
        // a=rtpmap:99 opus/48000/2
        if (const auto payloadId = parse_u32(value); payloadId.has_value() && is_valid_payload_id(payloadId.value())) {
            if (props.size() == 1 && isInMediaSection) {
                const auto posSlash = props[0].find('/');
                if (posSlash != std::string::npos) {
                    const auto codecString = props[0].substr(0, posSlash);
                    if (const auto codec = parse_codec(codecString); codec.has_value()) {
                        auto clockRateString = props[0].substr(posSlash + 1);
                        const auto posSlash2 = clockRateString.find('/');
                        if (posSlash2 != std::string::npos) {
                            clockRateString.resize(posSlash2);
                        }
                        if (const auto clockRate = parse_u32(clockRateString);
                            clockRate.has_value() && clockRate >= 10000u) {
                            const auto payloadIdValue = static_cast<uint8_t>(payloadId.value());
                            const auto payloadState = mediaState.getPayloadState(payloadIdValue);
                            if (payloadState) {
                                payloadState->codec = codec;
                                payloadState->clockRate = clockRate.value();
                                payloadState->payloadId = payloadIdValue;
                            }
                        }
                    }
                }
            }
        }
    } else if (key == "fmtp") {
        // a=fmtp:100 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f
        if (const auto payloadId = parse_u32(value);
            payloadId.has_value() && is_valid_payload_id(payloadId.value()) && isInMediaSection) {
            if (props.size() == 1) {
                const auto payloadIdValue = static_cast<uint8_t>(payloadId.value());

                std::unordered_map<std::string, std::string> map;
                parse_map(props[0], map);

                if (const auto iter1 = map.find("apt"); iter1 != map.end()) {
                    const auto payloadState = mediaState.getPayloadState(payloadIdValue);
                    if (payloadState && payloadState->codec == Codec::Rtx) {
                        if (const auto referencedPayloadId = parse_u32(iter1->second);
                            referencedPayloadId.has_value() && is_valid_payload_id(referencedPayloadId.value())) {
                            const auto referencedPayloadIdValue = static_cast<uint8_t>(referencedPayloadId.value());
                            const auto referencedPayloadState = mediaState.getPayloadState(referencedPayloadIdValue);
                            if (referencedPayloadState) {
                                referencedPayloadState->rtxPayloadId = payloadIdValue;
                            }
                        }
                    }
                }

                const auto payloadState = mediaState.getPayloadState(payloadIdValue);
                if (payloadState) {
                    uint32_t profileLevelId = 0;
                    uint32_t minptime = 0;
                    bool stereo = false;

                    if (mediaState.mediaType.value() == MediaType::Video) {
                        if (const auto iter2 = map.find("profile-level-id"); iter2 != map.end()) {
                            if (const auto parsed = parse_u32(iter2->second, 16); parsed.has_value()) {
                                profileLevelId = parsed.value();
                            }
                        }
                    } else if (mediaState.mediaType.value() == MediaType::Audio) {
                        if (const auto iter2 = map.find("minptime"); iter2 != map.end()) {
                            if (const auto parsed = parse_u32(iter2->second); parsed.has_value()) {
                                minptime = parsed.value();
                            }
                        }
                        if (const auto iter3 = map.find("stereo"); iter3 != map.end()) {
                            if (const auto parsed = parse_u32(iter3->second); parsed.has_value()) {
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
        if (const auto payloadId = parse_u32(value);
            payloadId.has_value() && is_valid_payload_id(payloadId.value()) && isInMediaSection) {
            const auto payloadIdValue = static_cast<uint8_t>(payloadId.value());
            const auto payloadState = mediaState.getPayloadState(payloadIdValue);
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
        if (isInMediaSection && mediaState.mediaType.value() == MediaType::Video) {
            if (props.size() == 1 && props[0] == "recv") {
                mediaState.ridList.push_back(value);
            }
        }
    } else if (key == "simulcast") {
        // a=simulcast recv low;mid;hi
        if (isInMediaSection && mediaState.mediaType.value() == MediaType::Video) {
            if (value == "recv" && props.size() == 1) {
                const auto offerLayerList = offer->getVideoSimulcastLayerList(mediaState.mediaId);
                if (offerLayerList.has_value() && !offerLayerList->empty()) {
                    const auto ridList = split_list(props[0]);
                    for (const auto& ridName : ridList) {
                        mediaState.addSimulcastLayer(offerLayerList.value(), ridName);
                    }
                }
            }
        }
    } else if (key == "candidate") {
        // a=candidate:182981660 1 udp 2130706431 99.181.107.72 443 typ host
        if (props.size() >= 7) {
            if (props[0] == "1" && props[1] == "udp" && props[5] == "typ" && props[6] == "host") {
                if (const auto port = parse_u32(props[4]); port.has_value() && port > 0u && port <= 65536u) {
                    Host host{};

                    const auto& addrStr = props[3];

                    if (addrStr.find('.') != std::string::npos) {
                        if (inet_pton(AF_INET, addrStr.c_str(), &host.addr.sin_ipv4.sin_addr) > 0) {

                            host.addr.ss.ss_family = AF_INET;
                            host.addr.sin_ipv4.sin_port = htons(static_cast<uint16_t>(port.value()));

                            if (std::find_if(hostList.begin(), hostList.end(), [host](const Host& it) {
                                    return host.addr == it.addr;
                                }) == hostList.end()) {
                                hostList.push_back(host);
                            }
                        }
                    } else if (addrStr.find(':') != std::string::npos) {
                        if (inet_pton(AF_INET6, addrStr.c_str(), &host.addr.sin_ipv6.sin6_addr) > 0) {

                            host.addr.ss.ss_family = AF_INET6;
                            host.addr.sin_ipv6.sin6_port = htons(static_cast<uint16_t>(port.value()));

                            if (std::find_if(hostList.begin(), hostList.end(), [host](const Host& it) {
                                    return host.addr == it.addr;
                                }) == hostList.end()) {
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
            const auto ssrcMedia = parse_u32(props[0]);
            const auto ssrcRtx = parse_u32(props[1]);
            if (isInMediaSection && ssrcMedia.has_value() && ssrcRtx.has_value()) {
                mediaState.ssrc = ssrcMedia.value();
                mediaState.rtxSsrc = ssrcRtx.value();
            }
        }
    } else if (key == "ssrc") {
        const auto ssrcMedia = parse_u32(value);
        if (isInMediaSection && ssrcMedia.has_value()) {
            if (std::find_if(mediaState.ssrcList.begin(), mediaState.ssrcList.end(), [ssrcMedia](uint32_t ssrc) {
                    return ssrc == ssrcMedia;
                }) == mediaState.ssrcList.end()) {
                mediaState.ssrcList.push_back(ssrcMedia.value());
            }
        }
    } else if (key == "sctp-port") {
        if (isInApplicationSection) {
            if (const auto port = parse_u32(value); port.has_value() && port > 0u && port <= 65535u) {
                sctpPort = static_cast<uint16_t>(port.value());
            }
        }
    } else if (key == "max-message-size") {
        if (isInApplicationSection) {
            if (const auto size = parse_u32(value); size.has_value()) {
                maxSctpMessageSize = size.value();
            }
        }
    }

    return Error::OK;
}

Error SdpAnswerParser::flush_m()
{
    if (isInMediaSection) {
        if (!mediaState.mediaType.has_value()) {
            return { Error::Code::InvalidData, "An unsupported media type" };
        }
        if (mediaState.mediaId.empty()) {
            return { Error::Code::InvalidData, "Media id cannot be empty" };
        }

        if (direction == Direction::Publish) {
            const auto publishSSRC = offer->getMediaSSRC(mediaState.mediaId);

            mediaState.ssrc = publishSSRC.first;
            mediaState.rtxSsrc = publishSSRC.second;
        }

        // Create media and track
        const auto media = mediaState.createMedia();

        auto track = mediaState.selectTrack(direction, media, selector);
        if (track == nullptr) {
            return { Error::Code::InvalidData, "Cannot create a media track" };
        }

        // If simulcast, create layer tracks
        std::vector<std::shared_ptr<Track>> simulcastTrackList;
        if (!mediaState.layerList.empty()) {
            simulcastTrackList = mediaState.makeSimulcastTrackList(offer, track);
            track.reset();
        }

        mediaList.push_back(media);

        if (track) {
            trackList.push_back(track);
        } else if (!simulcastTrackList.empty()) {
            trackList.insert(trackList.end(), simulcastTrackList.begin(), simulcastTrackList.end());
        }

        // We know that we have media
        hasMedia = true;
    }

    return Error::OK;
}

std::pair<std::shared_ptr<SdpAnswer>, Error> SdpAnswer::parse(const std::shared_ptr<SdpOffer>& offer,
                                                              const std::string& answer,
                                                              const std::shared_ptr<TrackSelector>& selector)
{
    SdpAnswerParser parser(offer, selector);
    return parser.parse(answer);
}

SdpAnswer::SdpAnswer(Direction direction,
                     const std::string& iceUFrag,
                     const std::string& icePassword,
                     const std::vector<Host>& hostList,
                     const std::vector<std::shared_ptr<Media>>& mediaList,
                     const std::vector<std::shared_ptr<Track>>& trackList,
                     bool isSetupActive,
                     const X509Hash& certHash,
                     bool hasDataChannel,
                     uint16_t sctpPort,
                     uint32_t maxMessageSize)
    : mDirection(direction)
    , mIceUFrag(iceUFrag)
    , mIcePassword(icePassword)
    , mHostList(hostList)
    , mMediaList(mediaList)
    , mTrackList(trackList)
    , mIsSetupActive(isSetupActive)
    , mCertHash(certHash)
    , mHasDataChannel(hasDataChannel)
    , mSctpPort(sctpPort)
    , mMaxMessageSize(maxMessageSize)
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

std::vector<Host> SdpAnswer::getHostList() const
{
    return mHostList;
}

std::vector<std::shared_ptr<Media>> SdpAnswer::getMediaList() const
{
    return mMediaList;
}

std::vector<std::shared_ptr<Track>> SdpAnswer::getTrackList() const
{
    return mTrackList;
}

bool SdpAnswer::isSetupActive() const
{
    return mIsSetupActive;
}

bool SdpAnswer::isVideoSimulcast() const
{
    return std::any_of(mTrackList.begin(), mTrackList.end(), [](const std::shared_ptr<Track>& track) {
        return track->getMediaType() == MediaType::Video && track->isSimulcast();
    });
}

const X509Hash& SdpAnswer::getCertificateHash() const
{
    return mCertHash;
}

bool SdpAnswer::hasDataChannel() const
{
    return mHasDataChannel;
}

uint16_t SdpAnswer::getSctpPort() const
{
    return mSctpPort;
}

uint32_t SdpAnswer::getMaxMessageSize() const
{
    return mMaxMessageSize;
}

} // namespace srtc
