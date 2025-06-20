#ifdef _WIN32
#include "srtc/srtc.h"
#include <wincrypt.h>
#undef X509_NAME
#undef X509_EXTENSIONS
#undef PKCS7_SIGNER_INFO
#endif

#include "srtc/byte_buffer.h"
#include "srtc/util.h"
#include "srtc/x509_certificate.h"

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

namespace srtc
{

struct X509CertificateImpl {
	EVP_PKEY* pkey = { nullptr };
	X509* x509 = { nullptr };
	ByteBuffer fp256bin;
	std::string fp256hex;
};

X509Certificate::X509Certificate()
	: mImpl(new X509CertificateImpl{})
{
	constexpr auto curveId = NID_X9_62_prime256v1;

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
	mImpl->pkey = nullptr;

	const auto ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
	EVP_PKEY_keygen_init(ctx);
	EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, curveId);

	EVP_PKEY_keygen(ctx, &mImpl->pkey);
#else
	mImpl->pkey = EVP_PKEY_new();

	auto ec = EC_KEY_new_by_curve_name(curveId);
	EC_KEY_generate_key(ec);
	EVP_PKEY_assign_EC_KEY(mImpl->pkey, ec);
#endif

	mImpl->x509 = X509_new();

	ASN1_INTEGER_set(X509_get_serialNumber(mImpl->x509), 1);
	X509_gmtime_adj(X509_get_notBefore(mImpl->x509), 0);
	X509_gmtime_adj(X509_get_notAfter(mImpl->x509), 31536000L);

	X509_set_pubkey(mImpl->x509, mImpl->pkey);

	const auto name = X509_get_subject_name(mImpl->x509);

	X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char*)"US", -1, -1, 0);
	X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (unsigned char*)"MyCompany Inc.", -1, -1, 0);
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char*)"localhost", -1, -1, 0);

	X509_set_issuer_name(mImpl->x509, name);

	X509_sign(mImpl->x509, mImpl->pkey, EVP_sha256());

	// Fingerprint
	uint8_t fpBuf[32] = {};
	unsigned int fpSize = {};

	const auto digest = EVP_get_digestbyname("sha256");
	X509_digest(mImpl->x509, digest, fpBuf, &fpSize);

	mImpl->fp256bin = { fpBuf, fpSize };
	mImpl->fp256hex = bin_to_hex(fpBuf, fpSize);

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
	EVP_PKEY_CTX_free(ctx);
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

const ByteBuffer& X509Certificate::getSha256FingerprintBin() const
{
	return mImpl->fp256bin;
}

std::string X509Certificate::getSha256FingerprintHex() const
{
	return mImpl->fp256hex;
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

} // namespace srtc
