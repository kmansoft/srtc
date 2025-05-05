#include "srtc/rtp_std_extensions.h"

namespace srtc {

const std::string RtpStandardExtensions::kExtSdesMid =
        "urn:ietf:params:rtp-hdrext:sdes:mid";
const std::string RtpStandardExtensions::kExtSdesRtpStreamId =
        "urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id";
const std::string RtpStandardExtensions::kExtSdesRtpRepairedStreamId =
        "urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id";
const std::string RtpStandardExtensions::kExtGoogleVLA =
        "http://www.webrtc.org/experiments/rtp-hdrext/video-layers-allocation00";
const std::string RtpStandardExtensions::kExtGoogleTWCC =
        "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01";
}
