#pragma once

#include "srtc/srtp_util.h"

#include <string>

namespace srtc
{

class SrtpKeys
{
public:
    SrtpKeys(ProfileId profileId,
             const std::string& profileIdName,
             bool isSetupActive,
             const uint8_t* srtpClientKey,
             const uint8_t* srtpServerKey,
             const uint8_t* srtpClientSalt,
             const uint8_t* srtpServerSalt,
             size_t srtpKeySize,
             size_t srtpSaltSize);
    ~SrtpKeys();

    const ProfileId profileId;
    const std::string profileIdName;
    CryptoBytes sendMasterKey, sendMasterSalt;
    CryptoBytes receiveMasterKey, receiveMasterSalt;
};

} // namespace srtc
