#include "srtc/srtp_keys.h"

namespace srtc
{

SrtpKeys::SrtpKeys(ProfileId profileId,
                   const std::string& profileIdName,
                   bool isSetupActive,
                   const uint8_t* srtpClientKey,
                   const uint8_t* srtpServerKey,
                   const uint8_t* srtpClientSalt,
                   const uint8_t* srtpServerSalt,
                   size_t srtpKeySize,
                   size_t srtpSaltSize)
    : profileId(profileId)
    , profileIdName(profileIdName)
{
    if (isSetupActive) {
        sendMasterKey.assign(srtpServerKey, srtpKeySize);
        sendMasterSalt.assign(srtpServerSalt, srtpSaltSize);

        receiveMasterKey.assign(srtpClientKey, srtpKeySize);
        receiveMasterSalt.assign(srtpClientSalt, srtpSaltSize);
    } else {
        sendMasterKey.assign(srtpClientKey, srtpKeySize);
        sendMasterSalt.assign(srtpClientSalt, srtpSaltSize);

        receiveMasterKey.assign(srtpServerKey, srtpKeySize);
        receiveMasterSalt.assign(srtpServerSalt, srtpSaltSize);
    }
}

SrtpKeys::~SrtpKeys() = default;

} // namespace srtc
