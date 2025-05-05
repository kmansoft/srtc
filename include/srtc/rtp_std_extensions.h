#pragma once

#include <string>

namespace srtc {

class RtpStandardExtensions {
public:
    static const std::string kExtSdesMid;
    static const std::string kExtSdesRtpStreamId;
    static const std::string kExtSdesRtpRepairedStreamId;
    static const std::string kExtGoogleVLA;
    static const std::string kExtGoogleTWCC;
};

}
