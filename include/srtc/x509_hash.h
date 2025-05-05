#pragma once

#include "srtc/byte_buffer.h"

#include <string>

namespace srtc
{

class X509Hash
{
public:
    X509Hash(const std::string& alg, const ByteBuffer& bin, const std::string& hex);
    ~X509Hash() = default;

    X509Hash(const X509Hash& hash);

    [[nodiscard]] std::string getAlg() const;
    [[nodiscard]] const ByteBuffer& getBin() const;
    [[nodiscard]] std::string getHex() const;

private:
    const std::string mAlg;
    const ByteBuffer mBin;
    const std::string mHex;
};

} // namespace srtc
