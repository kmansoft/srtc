#pragma once

#include "srtc/packetizer.h"

namespace srtc {

class PacketizerH264 final : public Packetizer {
public:
    PacketizerH264();
    ~PacketizerH264() override;
};

}
