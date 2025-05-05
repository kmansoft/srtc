#pragma once

#include <string>

struct evp_pkey_st;
struct x509_st;

namespace srtc
{

class ByteBuffer;

struct X509CertificateImpl;

class X509Certificate
{
public:
    X509Certificate();
    ~X509Certificate();

    [[nodiscard]] struct evp_pkey_st* getPrivateKey() const;
    [[nodiscard]] struct x509_st* getCertificate() const;

    [[nodiscard]] std::string getSha256FingerprintHex() const;
    [[nodiscard]] const ByteBuffer& getSha256FingerprintBin() const;

private:
    X509CertificateImpl* const mImpl;
};

} // namespace srtc
