#include "srtc/x509_hash.h"

namespace srtc {

X509Hash::X509Hash(const std::string& alg,
                   const ByteBuffer& bin,
                   const std::string& hex)
   : mAlg(alg)
   , mBin(bin.copy())
   , mHex(hex)
{
}

X509Hash::X509Hash(const X509Hash& hash)
    : mAlg(hash.mAlg)
    , mBin(hash.mBin.copy())
    , mHex(hash.mHex)
{
}

std::string X509Hash::getAlg() const
{
    return mAlg;
}

const ByteBuffer& X509Hash::getBin() const
{
    return mBin;
}

std::string X509Hash::getHex() const
{
    return mHex;
}

}
