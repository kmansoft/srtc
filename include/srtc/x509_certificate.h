#pragma once

#include <string>

#if defined(SRTC_USE_OPENSSL)
struct evp_pkey_st;
struct x509_st;
#elif defined(SRTC_USE_MBEDTLS)
struct mbedtls_pk_context;
struct mbedtls_x509_crt;
#endif

namespace srtc
{

class ByteBuffer;

struct X509CertificateImpl;

class X509Certificate
{
public:
    X509Certificate();
    ~X509Certificate();

#if defined(SRTC_USE_OPENSSL)
    [[nodiscard]] struct evp_pkey_st* getPrivateKey() const;
    [[nodiscard]] struct x509_st* getCertificate() const;
#elif defined(SRTC_USE_MBEDTLS)
    [[nodiscard]] struct mbedtls_pk_context* getPrivateKey() const;
    [[nodiscard]] struct mbedtls_x509_crt* getCertificate() const;
#endif

    [[nodiscard]] std::string getSha256FingerprintHex() const;
    [[nodiscard]] const ByteBuffer& getSha256FingerprintBin() const;

private:
    X509CertificateImpl* const mImpl;
};

} // namespace srtc
