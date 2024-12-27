#pragma once

#include <string>

struct evp_pkey_st;
struct x509_st;

namespace srtc {

struct X509CertificateImpl;

class X509Certificate {
public:
    X509Certificate();
    ~X509Certificate();

    struct evp_pkey_st* getPrivateKey() const;
    struct x509_st* getCertificate() const;

    std::string getSha256Fingerprint() const;

private:
    X509CertificateImpl* const mImpl;
};

}
