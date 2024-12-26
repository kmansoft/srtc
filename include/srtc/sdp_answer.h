#pragma once

#include "srtc/srtc.h"
#include "srtc/error.h"

#include <memory>

namespace srtc {

class SdpAnswer {
public:

    static Error parse(const std::string& answer, std::shared_ptr<SdpAnswer>& outAnswer);

    ~SdpAnswer();

private:
    SdpAnswer();
};

}
