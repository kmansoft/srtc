#include "srtc/x509_certificate.h"

#include <openssl/x509.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

#ifdef ANDROID
#include <android/log.h>
#endif

namespace srtc {

struct X509CertificateImpl {
    EVP_PKEY* pkey = { nullptr };
    X509* x509 = {nullptr };
    std::string fp256;
};

X509Certificate::X509Certificate()
    : mImpl(new X509CertificateImpl{})
{
    // https://stackoverflow.com/questions/256405/programmatically-create-x509-certificate-using-openssl

    mImpl->pkey = EVP_PKEY_new();

    BIGNUM e;
    BN_init(&e);
    BN_set_word(&e, RSA_F4);

    auto rsa = RSA_new();
    RSA_generate_key_ex(rsa, 2048, &e, nullptr);
    EVP_PKEY_assign_RSA(mImpl->pkey, rsa);

    BN_free(&e);

    mImpl->x509 = X509_new();

    ASN1_INTEGER_set(X509_get_serialNumber(mImpl->x509), 1);
    X509_gmtime_adj(X509_get_notBefore(mImpl->x509), 0);
    X509_gmtime_adj(X509_get_notAfter(mImpl->x509), 31536000L);

    X509_set_pubkey(mImpl->x509, mImpl->pkey);

    const auto name = X509_get_subject_name(mImpl->x509);

    X509_NAME_add_entry_by_txt(name, "C",  MBSTRING_ASC,
                               (unsigned char *)"US", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O",  MBSTRING_ASC,
                               (unsigned char *)"MyCompany Inc.", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               (unsigned char *)"localhost", -1, -1, 0);

    X509_set_issuer_name(mImpl->x509, name);

    X509_sign(mImpl->x509, mImpl->pkey, EVP_sha1());

    // Fingerprint
    uint8_t fpBuf[32] = { };
    unsigned int fpSize = { };

    const auto digest = EVP_get_digestbyname("sha256");
    X509_digest(mImpl->x509, digest, fpBuf, &fpSize);

    std::string fpHex;
    for (unsigned int i = 0; i < fpSize; i += 1) {
        static const char* const ALPHABET = "0123456789abcdef";
        fpHex += (ALPHABET[(fpBuf[i] >> 4) & 0x0F]);
        fpHex += (ALPHABET[(fpBuf[i]) & 0x0F]);
        if (i != fpSize -1) {
            fpHex += ':';
        }
    }

    mImpl->fp256 = fpHex;

#ifdef ANDROID
    // debug
    const auto bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(bio, mImpl->x509);

    const uint8_t* outContents = nullptr;
    size_t outLen = -1;
    BIO_mem_contents(bio, &outContents, &outLen);

    std::string str { outContents, outContents + outLen };

    __android_log_print(ANDROID_LOG_INFO, "srtc",
                        "Certificate:\n%s", str.substr(0, 512).c_str());
    __android_log_print(ANDROID_LOG_INFO, "srtc",
                        "Certificate:\n%s", str.substr(512).c_str());

    BIO_free(bio);
#endif
}

struct evp_pkey_st* X509Certificate::getPrivateKey() const
{
    return mImpl->pkey;
}

struct x509_st* X509Certificate::getCertificate() const
{
    return mImpl->x509;
}

std::string X509Certificate::getSha256Fingerprint() const
{
    return mImpl->fp256;
}

X509Certificate::~X509Certificate()
{
    if (mImpl->pkey) {
        EVP_PKEY_free(mImpl->pkey);
    }
    if (mImpl->x509) {
        X509_free(mImpl->x509);
    }

    delete mImpl;
}

}
