#include <sstream>
#include <vector>
#include <unordered_map>

#include <arpa/inet.h>

#include "srtc/sdp_answer.h"
#include "srtc/extension_map.h"
#include "srtc/track.h"

namespace {

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

void parse_map(const std::string& line,
               std::unordered_map<std::string, std::string>& outMap)
{
    size_t curr = 0;
    while (true) {
        const auto next = line.find(';', curr);
        const auto prop = line.substr(curr, next);

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

int parse_int(const std::string& s, int radix = 10) {
    char* endptr = nullptr;
    long l = std::strtol(s.c_str(), &endptr, radix);
    if (endptr == s.c_str() + s.size()) {
        return static_cast<int>(l);
    }
    return -1;
}

}

namespace srtc {

Error SdpAnswer::parse(const std::string& answer, std::shared_ptr<SdpAnswer> &outAnswer)
{
    std::stringstream ss(answer);

    auto isIceLite = false;
    std::string iceUFrag, icePassword;

    ExtensionMap extensionMap;
    std::vector<Host> hostList;

    int videoTrackId = -1, videoPayloadType = -1;
    int audioTrackId = -1, audioPayloadType = -1;

    auto videoCodec = srtc::Codec::Unknown;
    int videoProfileId = -1, videoLevel = -1;

    auto audioCodec = srtc::Codec::Unknown;

    auto isSetupActive = false;

    while (!ss.eof()) {
        std::string line;
        std::getline(ss, line);

        if (const auto sz = line.size()) {
            if (line[sz-1] == '\r') {
                line.erase(sz - 1, sz);
            }
        }

        if (!line.empty()) {
            std::string tag, key, value;
            std::vector<std::string> props;

            parse_line(line, tag, key, value, props);

            if (tag == "a") {
                if (key == "ice-lite") {
                    isIceLite = true;
                } else if (key == "ice-ufrag") {
                    iceUFrag = value;
                } else if (key == "ice-pwd") {
                    icePassword = value;
                } else if (key == "setup") {
                    isSetupActive = value == "active";
                } else if (key == "extmap") {
                    const auto id = parse_int(value);
                    if (id >= 0) {
                        if (props.size() == 1) {
                            extensionMap.set(id, props[0]);
                        }
                    }
                } else if (key == "rtpmap") {
                    if (parse_int(value) == videoPayloadType) {
                        // a=rtpmap:100 H264/90000
                        if (props.size() == 1) {
                            const auto posSlash = props[0].find('/');
                            if (posSlash != std::string::npos) {
                                const auto codec = props[0].substr(0, posSlash);
                                if (codec == "H264") {
                                    videoCodec = srtc::Codec::H264;
                                }
                            }
                        }
                    } else if (parse_int(value) == audioPayloadType) {
                        if (props.size() == 1) {
                            const auto posSlash = props[0].find('/');
                            if (posSlash != std::string::npos) {
                                const auto codec = props[0].substr(0, posSlash);
                                if (codec == "opus") {
                                    audioCodec = srtc::Codec::Opus;
                                }
                            }
                        }
                    }
                } else if (key == "fmtp") {
                    if (parse_int(value) == videoPayloadType) {
                        // a=fmtp:100 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f
                        if (props.size() == 1) {
                            std::unordered_map<std::string, std::string> map;
                            parse_map(props[0], map);

                            if (const auto iter = map.find("profile-level-id"); iter != map.end()) {
                                const auto profileLevelId = parse_int(iter->second, 16);
                                if (profileLevelId > 0) {
                                    videoProfileId = profileLevelId >> 16;
                                    videoLevel = profileLevelId & 0xFFFF;
                                }
                            }
                        }
                    }
                } else if (key == "candidate") {
                    // a=candidate:182981660 1 udp 2130706431 99.181.107.72 443 typ host
                    if (props.size() == 7) {
                        if (props[0] == "1" && props[1] == "udp" && props[5] == "typ" && props[6] == "host") {
                            if (const auto port = parse_int(props[4]); port > 0 && port < 63536) {
                                Host host { .family = AF_UNSPEC, .port = port };

                                const auto& addrStr = props[3];
                                if (addrStr.find('.') != std::string::npos) {
                                    if (inet_pton(AF_INET, addrStr.c_str(), &host.host.ipv4) > 0) {
                                        host.family = AF_INET;
                                    }
                                } else if (addrStr.find(':') != std::string::npos) {
                                    // TODO - add support for IPv6
//                                    if (inet_pton(AF_INET6, addrStr.c_str(), &host.host.ipv6) > 0) {
//                                        host.family = AF_INET6;
//                                    }
                                }

                                if (host.family != AF_UNSPEC) {
                                    hostList.push_back(host);
                                }
                            }
                        }
                    }
                }
            } else if (tag == "m") {
                // "m=video 0 UDP/TLS/RTP/SAVPF 100"
                if (props.size() < 2 || props[1] != "UDP/TLS/RTP/SAVPF") {
                    return Error { Error::Code::InvalidData, "Only SAVPF over DTLS is supported" };
                }

                if (key == "video") {
                    if (props.size() >= 3) {
                        videoTrackId = parse_int(props[0]);
                        videoPayloadType = parse_int(props[2]);
                    }
                } else if (key == "audio") {
                    if (props.size() >= 3) {
                        audioTrackId = parse_int(props[0]);
                        audioPayloadType = parse_int(props[2]);
                    }
                }
            }
        }
    }

    if (!isIceLite) {
        return { Error::Code::InvalidData, "Only ice-lite is supported" };
    }
    if (hostList.empty()) {
        return { Error::Code::InvalidData, "No hosts to connect to" };
    }
    if (videoTrackId < 0) {
        return { Error::Code::InvalidData, "No video track" };
    }
    if (videoCodec == Codec::Unknown) {
        return { Error::Code::InvalidData, "No video codec" };
    }

    const auto videoTrack =
            std::make_shared<Track>(videoTrackId, videoPayloadType, videoCodec,
                                                    videoProfileId, videoLevel);
    const auto audioTrack = audioTrackId >= 0 ?
                            std::make_shared<Track>(audioTrackId, audioPayloadType, audioCodec) : nullptr;

    outAnswer = std::make_shared<SdpAnswer>(iceUFrag, icePassword, extensionMap,
                                            hostList,
                                            videoTrack, audioTrack,
                                            isSetupActive);

    return Error::OK;
}

SdpAnswer::SdpAnswer(const std::string& iceUFrag,
                     const std::string& icePassword,
                     const ExtensionMap& extensionMap,
                     const std::vector<Host>& hostList,
                     const std::shared_ptr<Track>& videoTrack,
                     const std::shared_ptr<Track>& audioTrack,
                     bool isSetupActive)
     : mIceUFrag(iceUFrag)
     , mIcePassword(icePassword)
     , mExtensionMap(extensionMap)
     , mHostList(hostList)
     , mVideoTrack(videoTrack)
     , mAudioTrack(audioTrack)
     , mIsSetupActive(isSetupActive)
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

ExtensionMap SdpAnswer::getExtensionMap() const
{
    return mExtensionMap;
}

std::vector<Host> SdpAnswer::getHostList() const
{
    return mHostList;
}

std::shared_ptr<Track> SdpAnswer::getVideoTrack() const
{
    return mVideoTrack;
}

std::shared_ptr<Track> SdpAnswer::getAudioTrack() const
{
    return mAudioTrack;
}

bool SdpAnswer::isSetupActive() const
{
    return mIsSetupActive;
}

}
