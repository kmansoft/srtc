#ifdef _WIN32
#include "srtc/srtc.h"
#include <wincrypt.h>
#undef X509_NAME
#undef X509_EXTENSIONS
#undef PKCS7_SIGNER_INFO
#endif

#include "srtc/byte_buffer.h"
#include "srtc/logging.h"
#include "srtc/util.h"
#include "srtc/x509_certificate.h"

#if defined(SRTC_USE_OPENSSL)
#include <openssl/evp.h>
#include <openssl/x509.h>
#elif defined(SRTC_USE_MBEDTLS)
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>
#if defined(MBEDTLS_PSA_CRYPTO_C)
#include <psa/crypto.h>
#endif

#include <ctime>
#endif

#define LOG(level, ...) srtc::log(level, "X509Certificate", __VA_ARGS__)

namespace
{

#if defined(SRTC_USE_MBEDTLS)

constexpr auto kValiditySeconds = 31536000L;
constexpr char kSubjectName[] = "C=US,O=MyCompany Inc.,CN=localhost";
constexpr char kDrbgPersonalization[] = "srtc_x509_certificate";

// mbedTLS wants validity as "YYYYMMDDhhmmss" in UTC
std::string format_validity_time(std::time_t t)
{
	std::tm tm = {};
#ifdef _WIN32
	gmtime_s(&tm, &t);
#else
	gmtime_r(&t, &tm);
#endif

	char buf[16] = {};
	std::strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &tm);
	return buf;
}

#endif

} // namespace

namespace srtc
{

#if defined(SRTC_USE_OPENSSL)

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

	mImpl->pkey = nullptr;

	const auto ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
	EVP_PKEY_keygen_init(ctx);
	EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, curveId);

	EVP_PKEY_keygen(ctx, &mImpl->pkey);

	mImpl->x509 = X509_new();

	ASN1_INTEGER_set(X509_get_serialNumber(mImpl->x509), 1);
	X509_gmtime_adj(X509_get_notBefore(mImpl->x509), 0);
	X509_gmtime_adj(X509_get_notAfter(mImpl->x509), 31536000L);

	X509_set_pubkey(mImpl->x509, mImpl->pkey);

	// OpenSSL 4 returns a const name from X509_get_subject_name, so build our own (set_*_name make copies)
	const auto name = X509_NAME_new();

	X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char*)"US", -1, -1, 0);
	X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (unsigned char*)"MyCompany Inc.", -1, -1, 0);
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char*)"localhost", -1, -1, 0);

	X509_set_subject_name(mImpl->x509, name);
	X509_set_issuer_name(mImpl->x509, name);

	X509_NAME_free(name);

	X509_sign(mImpl->x509, mImpl->pkey, EVP_sha256());

	// Fingerprint
	uint8_t fpBuf[32] = {};
	unsigned int fpSize = {};

	const auto digest = EVP_get_digestbyname("sha256");
	X509_digest(mImpl->x509, digest, fpBuf, &fpSize);

	mImpl->fp256bin = { fpBuf, fpSize };
	mImpl->fp256hex = bin_to_hex(fpBuf, fpSize);

	EVP_PKEY_CTX_free(ctx);
}

struct evp_pkey_st* X509Certificate::getPrivateKey() const
{
	return mImpl->pkey;
}

struct x509_st* X509Certificate::getCertificate() const
{
	return mImpl->x509;
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

#elif defined(SRTC_USE_MBEDTLS)

struct X509CertificateImpl {
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context drbg;
	mbedtls_pk_context pkey;
	mbedtls_x509_crt x509;
	ByteBuffer fp256bin;
	std::string fp256hex;
};

X509Certificate::X509Certificate()
	: mImpl(new X509CertificateImpl{})
{
	mbedtls_entropy_init(&mImpl->entropy);
	mbedtls_ctr_drbg_init(&mImpl->drbg);
	mbedtls_pk_init(&mImpl->pkey);
	mbedtls_x509_crt_init(&mImpl->x509);

#if defined(MBEDTLS_PSA_CRYPTO_C)
	// Key generation and signing can go through PSA depending on the mbedTLS config
	psa_crypto_init();
#endif

	mbedtls_x509write_cert crt;
	mbedtls_x509write_crt_init(&crt);

	const auto now = std::time(nullptr);
	const auto notBefore = format_validity_time(now);
	const auto notAfter = format_validity_time(now + kValiditySeconds);

	uint8_t serial[] = { 1 };

	// mbedtls_x509write_crt_der writes at the end of the buffer
	uint8_t der[1024];
	size_t derSize = 0;

	// Key
	auto r = mbedtls_ctr_drbg_seed(&mImpl->drbg,
								   mbedtls_entropy_func,
								   &mImpl->entropy,
								   reinterpret_cast<const unsigned char*>(kDrbgPersonalization),
								   sizeof(kDrbgPersonalization) - 1);
	if (r == 0) {
		r = mbedtls_pk_setup(&mImpl->pkey, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
	}
	if (r == 0) {
		r = mbedtls_ecp_gen_key(
			MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(mImpl->pkey), mbedtls_ctr_drbg_random, &mImpl->drbg);
	}

	// Self signed certificate
	if (r == 0) {
		mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
		mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
		mbedtls_x509write_crt_set_subject_key(&crt, &mImpl->pkey);
		mbedtls_x509write_crt_set_issuer_key(&crt, &mImpl->pkey);

		r = mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial));
	}
	if (r == 0) {
		r = mbedtls_x509write_crt_set_validity(&crt, notBefore.c_str(), notAfter.c_str());
	}
	if (r == 0) {
		r = mbedtls_x509write_crt_set_subject_name(&crt, kSubjectName);
	}
	if (r == 0) {
		r = mbedtls_x509write_crt_set_issuer_name(&crt, kSubjectName);
	}
	if (r == 0) {
		r = mbedtls_x509write_crt_der(&crt, der, sizeof(der), mbedtls_ctr_drbg_random, &mImpl->drbg);
		if (r > 0) {
			derSize = static_cast<size_t>(r);
			r = 0;
		}
	}

	const auto derStart = der + sizeof(der) - derSize;

	if (r == 0) {
		r = mbedtls_x509_crt_parse_der(&mImpl->x509, derStart, derSize);
	}

	// Fingerprint, same as X509_digest: SHA-256 over the DER encoding
	if (r == 0) {
		uint8_t fpBuf[32] = {};
		r = mbedtls_sha256(derStart, derSize, fpBuf, 0 /* not sha-224 */);

		if (r == 0) {
			mImpl->fp256bin = { fpBuf, sizeof(fpBuf) };
			mImpl->fp256hex = bin_to_hex(fpBuf, sizeof(fpBuf));
		}
	}

	if (r != 0) {
		LOG(SRTC_LOG_E, "Failed to generate the certificate: -0x%04x", static_cast<unsigned int>(-r));
	}

	mbedtls_x509write_crt_free(&crt);
}

struct mbedtls_pk_context* X509Certificate::getPrivateKey() const
{
	return &mImpl->pkey;
}

struct mbedtls_x509_crt* X509Certificate::getCertificate() const
{
	return &mImpl->x509;
}

X509Certificate::~X509Certificate()
{
	mbedtls_x509_crt_free(&mImpl->x509);
	mbedtls_pk_free(&mImpl->pkey);
	mbedtls_ctr_drbg_free(&mImpl->drbg);
	mbedtls_entropy_free(&mImpl->entropy);

	delete mImpl;
}

#endif

const ByteBuffer& X509Certificate::getSha256FingerprintBin() const
{
	return mImpl->fp256bin;
}

std::string X509Certificate::getSha256FingerprintHex() const
{
	return mImpl->fp256hex;
}

} // namespace srtc
