#include "crypto_dtls_session.h"
#include "crypto_provider.h"
#include "crypto_util.h"

#include "srtc/logging.h"
#include "srtc/util.h"
#include "srtc/x509_certificate.h"

#include <mbedtls/build_info.h>
#include <mbedtls/error.h>
#include <mbedtls/md.h>
#include <mbedtls/x509_crt.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstring>

#define LOG(level, ...) srtc::log(level, "DtlsSession", __VA_ARGS__)

namespace
{

// mbedTLS 3.x supports only these SRTP profiles, there are no AEAD (GCM) ones
const mbedtls_ssl_srtp_profile kSrtpProfileList[] = { MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80,
                                                      MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_32,
                                                      MBEDTLS_TLS_SRTP_UNSET };

const char* const kSrtpExporterLabel = "EXTRACTOR-dtls_srtp";

constexpr char kDrbgPersonalization[] = "srtc_dtls_session";

constexpr uint16_t kMtu = 1200;

// Max DTLS record plaintext size
constexpr size_t kMaxReadSize = 16384;

constexpr auto kMinRetransmitTimeout = std::chrono::milliseconds(50);
constexpr auto kMaxInitialRetransmitTimeout = std::chrono::milliseconds(1000);
// mbedTLS doubles the timeout from the initial value and fails the handshake once it would exceed this
constexpr auto kMaxHandshakeTimeout = std::chrono::milliseconds(16000);

struct SrtpProfileInfo {
    srtc::ProfileId id;
    const char* name;
    size_t keySize;
    size_t saltSize;
};

std::optional<SrtpProfileInfo> get_srtp_profile_info(mbedtls_ssl_srtp_profile profile)
{
    switch (profile) {
    case MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80:
        return SrtpProfileInfo{ srtc::ProfileId::kAES_128_CM_SHA1_80, "AES_128_CM_SHA1_80", 16, 14 };
    case MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_32:
        return SrtpProfileInfo{ srtc::ProfileId::kAES_128_CM_SHA1_32, "AES_128_CM_SHA1_32", 16, 14 };
    default:
        return std::nullopt;
    }
}

const mbedtls_md_info_t* get_digest(const std::string& alg)
{
    std::string lower = alg;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });

    if (lower == "sha-256" || lower == "sha256") {
        return mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    } else if (lower == "sha-384" || lower == "sha384") {
        return mbedtls_md_info_from_type(MBEDTLS_MD_SHA384);
    } else if (lower == "sha-512" || lower == "sha512") {
        return mbedtls_md_info_from_type(MBEDTLS_MD_SHA512);
    } else if (lower == "sha-1" || lower == "sha1") {
        return mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    }

    return nullptr;
}

std::string get_mbedtls_error(int r)
{
    char hex[16];
    std::snprintf(hex, sizeof(hex), "-0x%04x", static_cast<unsigned int>(-r));

#if defined(MBEDTLS_ERROR_C)
    char buf[160] = {};
    mbedtls_strerror(r, buf, sizeof(buf));
    return std::string(buf) + " (" + hex + ")";
#else
    return hex;
#endif
}

srtc::Error make_error(const std::string& message, int r)
{
    return { srtc::Error::Code::InvalidData, message + ": " + get_mbedtls_error(r) };
}

} // namespace

namespace srtc::compat
{

std::pair<std::shared_ptr<DtlsSession>, Error> DtlsSession::create(bool isSetupActive,
                                                                   const std::shared_ptr<X509Certificate>& certificate,
                                                                   const X509Hash& expectedHash,
                                                                   const std::shared_ptr<RealScheduler>& scheduler,
                                                                   float iceRttMillis,
                                                                   const SendFn& sendFn,
                                                                   const ConnectSuccessFn& connectSuccessFn,
                                                                   const ConnectFailedFn& connectFailedFn)
{
    const auto session = std::make_shared<DtlsSession>(
        isSetupActive, certificate, expectedHash, scheduler, iceRttMillis, sendFn, connectSuccessFn, connectFailedFn);

    // Setup errors are reported from start(), so the caller always has a session and its state
    return { session, Error::OK };
}

DtlsSession::DtlsSession(bool isSetupActive,
                         const std::shared_ptr<X509Certificate>& certificate,
                         const X509Hash& expectedHash,
                         const std::shared_ptr<RealScheduler>& scheduler,
                         float iceRttMillis,
                         const SendFn& sendFn,
                         const ConnectSuccessFn& connectSuccessFn,
                         const ConnectFailedFn& connectFailedFn)
    : mIsSetupActive(isSetupActive)
    , mCertificate(certificate)
    , mExpectedHash(expectedHash)
    , mIceRttMillis(iceRttMillis)
    , mSendFn(sendFn)
    , mConnectSuccessFn(connectSuccessFn)
    , mConnectFailedFn(connectFailedFn)
    , mState(DtlsState::Inactive)
    , mEntropy()
    , mDrbg()
    , mConf()
    , mSsl()
    , mIsSslSetup(false)
    , mHasMasterSecret(false)
    , mMasterSecret()
    , mClientServerRandom()
    , mTlsPrfType(MBEDTLS_SSL_TLS_PRF_NONE)
    , mIsTimerActive(false)
    , mScheduler(scheduler)
{
    // These are safe to free even if never set up
    mbedtls_entropy_init(&mEntropy);
    mbedtls_ctr_drbg_init(&mDrbg);
    mbedtls_ssl_config_init(&mConf);
    mbedtls_ssl_init(&mSsl);
}

DtlsSession::~DtlsSession()
{
    // No close_notify here, the owner may be partially destroyed, use close() for that
    freeSsl();

    mbedtls_ctr_drbg_free(&mDrbg);
    mbedtls_entropy_free(&mEntropy);
}

Error DtlsSession::init()
{
    // mbedTLS 3.x with TLS 1.3 compiled in needs PSA initialized before the SSL layer is used
    initCryptoProvider();

    int r = mbedtls_ctr_drbg_seed(&mDrbg,
                                  mbedtls_entropy_func,
                                  &mEntropy,
                                  reinterpret_cast<const unsigned char*>(kDrbgPersonalization),
                                  sizeof(kDrbgPersonalization) - 1);
    if (r != 0) {
        return make_error("Cannot seed the random generator", r);
    }

    // We are the server when the remote is active
    r = mbedtls_ssl_config_defaults(&mConf,
                                    mIsSetupActive ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
    if (r != 0) {
        return make_error("Cannot set up the DTLS configuration", r);
    }

    // WebRTC uses DTLS 1.2
    mbedtls_ssl_conf_min_tls_version(&mConf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&mConf, MBEDTLS_SSL_VERSION_TLS1_2);

    mbedtls_ssl_conf_rng(&mConf, mbedtls_ctr_drbg_random, &mDrbg);

    // Optional means the server requests the client certificate, and neither side fails on self signed ones.
    // The certificate itself is required in onHandshakeCompleted(), where we check its fingerprint
    mbedtls_ssl_conf_authmode(&mConf, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_verify(&mConf, verify_cb, this);

    r = mbedtls_ssl_conf_own_cert(&mConf, mCertificate->getCertificate(), mCertificate->getPrivateKey());
    if (r != 0) {
        return make_error("Invalid certificate or private key", r);
    }

    r = mbedtls_ssl_conf_dtls_srtp_protection_profiles(&mConf, kSrtpProfileList);
    if (r != 0) {
        return make_error("Cannot set SRTP profiles", r);
    }

#if defined(MBEDTLS_SSL_SRV_C) && defined(MBEDTLS_SSL_DTLS_HELLO_VERIFY)
    if (mIsSetupActive) {
        // ICE has already verified the peer's address, and the default cookie callbacks fail the handshake
        mbedtls_ssl_conf_dtls_cookies(&mConf, nullptr, nullptr, nullptr);
    }
#endif

    // Initial retransmit timeout: twice the ICE round trip time
    {
        using namespace std::chrono;
        const auto iceRtt = duration_cast<milliseconds>(duration<float, std::milli>(mIceRttMillis));
        const auto initial = std::clamp<milliseconds>(2 * iceRtt, kMinRetransmitTimeout, kMaxInitialRetransmitTimeout);

        mbedtls_ssl_conf_handshake_timeout(
            &mConf, static_cast<uint32_t>(initial.count()), static_cast<uint32_t>(kMaxHandshakeTimeout.count()));

        LOG(SRTC_LOG_V, "DTLS initial retransmit timeout %lld ms", static_cast<long long>(initial.count()));
    }

    r = mbedtls_ssl_setup(&mSsl, &mConf);
    if (r != 0) {
        return make_error("Cannot set up the DTLS session", r);
    }
    mIsSslSetup = true;

    mbedtls_ssl_set_bio(&mSsl, this, bio_send, bio_recv, nullptr);
    mbedtls_ssl_set_timer_cb(&mSsl, this, timer_set, timer_get);
    mbedtls_ssl_set_mtu(&mSsl, kMtu);
    mbedtls_ssl_set_export_keys_cb(&mSsl, export_keys_cb, this);

    if (!mIsSetupActive) {
        // There is no hostname to check, the caller checks the fingerprint against the SDP instead.
        // Since 3.6.3, clients which verify certificates have to say so explicitly
        r = mbedtls_ssl_set_hostname(&mSsl, nullptr);
        if (r != 0) {
            return make_error("Cannot clear the DTLS hostname", r);
        }
    }

    return Error::OK;
}

void DtlsSession::freeSsl()
{
    Task::cancelHelper(mTaskRetransmit);
    mIsTimerActive = false;

    // Leaves them initialized, so this is safe to call more than once
    mbedtls_ssl_free(&mSsl);
    mbedtls_ssl_config_free(&mConf);
    mbedtls_ssl_init(&mSsl);
    mbedtls_ssl_config_init(&mConf);
    mIsSslSetup = false;

    mReceiveQueue.clear();
    mPeerCertificateDer.clear();

    secureZero(mMasterSecret, sizeof(mMasterSecret));
    secureZero(mClientServerRandom, sizeof(mClientServerRandom));
    mHasMasterSecret = false;
}

Error DtlsSession::start()
{
    if (mState != DtlsState::Inactive) {
        return { Error::Code::InvalidData, "The DTLS session cannot be started" };
    }

    if (const auto error = init(); error.isError()) {
        mState = DtlsState::Failed;
        freeSsl();
        return error;
    }

    LOG(SRTC_LOG_V, "Starting the DTLS handshake as %s", mIsSetupActive ? "server" : "client");

    mState = DtlsState::Activating;

    // As a client this sends the ClientHello, as a server it just waits for one
    const auto r = mbedtls_ssl_handshake(&mSsl);
    if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
        const auto error = make_error("Cannot start the DTLS handshake", r);
        mState = DtlsState::Failed;
        freeSsl();
        return error;
    }

    return Error::OK;
}

void DtlsSession::close()
{
    if (mIsSslSetup && mState == DtlsState::Completed) {
        // Sends close_notify through bio_send
        mbedtls_ssl_close_notify(&mSsl);
    }

    mState = DtlsState::Closed;
    freeSsl();
}

void DtlsSession::received(ByteBuffer&& buf)
{
    switch (mState) {
    case DtlsState::Activating:
        mReceiveQueue.push_back(std::move(buf));
        // This may destroy us
        onHandshakeStep();
        break;
    case DtlsState::Completed:
        // The caller is expected to call read()
        mReceiveQueue.push_back(std::move(buf));
        break;
    default:
        break;
    }
}

Error DtlsSession::send(const ByteBuffer& buf)
{
    if (mState != DtlsState::Completed || !mIsSslSetup) {
        return { Error::Code::InvalidData, "The DTLS session is not connected" };
    }

    const auto r = mbedtls_ssl_write(&mSsl, buf.data(), buf.size());
    if (r < 0) {
        return make_error("mbedtls_ssl_write failed", r);
    }

    return Error::OK;
}

std::pair<bool, Error> DtlsSession::read(ByteBuffer& buf)
{
    buf.clear();

    if (mState != DtlsState::Completed || !mIsSslSetup) {
        return { false, { Error::Code::InvalidData, "The DTLS session is not connected" } };
    }

    buf.reserve(kMaxReadSize);

    // This also lets mbedTLS respond to retransmitted handshake flights from the peer
    const auto r = mbedtls_ssl_read(&mSsl, buf.data(), kMaxReadSize);
    if (r > 0) {
        buf.resize(static_cast<size_t>(r));
        return { false, Error::OK };
    }

    if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        LOG(SRTC_LOG_V, "Received DTLS close_notify, peer disconnected gracefully");
        mState = DtlsState::Closed;
        return { true, Error::OK };
    }

    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
        // No more data for now
        return { false, Error::OK };
    }

    LOG(SRTC_LOG_V, "DTLS connection lost: %s", get_mbedtls_error(r).c_str());
    mState = DtlsState::Closed;
    return { false, { Error::Code::InvalidData, "DTLS connection lost unexpectedly" } };
}

std::pair<std::optional<SrtpKeys>, Error> DtlsSession::getSrtpKeys() const
{
    if (mState != DtlsState::Completed || !mIsSslSetup) {
        return { std::nullopt, { Error::Code::InvalidData, "The DTLS session is not connected" } };
    }

    if (!mHasMasterSecret) {
        return { std::nullopt, { Error::Code::InvalidData, "The DTLS master secret was not exported" } };
    }

    // https://datatracker.ietf.org/doc/html/rfc5764#section-4.2
    mbedtls_dtls_srtp_info srtpInfo = {};
    mbedtls_ssl_get_dtls_srtp_negotiation_result(&mSsl, &srtpInfo);

    const auto info = get_srtp_profile_info(srtpInfo.MBEDTLS_PRIVATE(chosen_dtls_srtp_profile));
    if (!info) {
        return { std::nullopt, { Error::Code::InvalidData, "Unsupported SRTP profile" } };
    }

    // client key, server key, client salt, server salt
    const auto materialSize = (info->keySize + info->saltSize) * 2;
    ByteBuffer material(materialSize);

    // The RFC 5705 exporter for TLS 1.2: PRF(master_secret, label, client_random + server_random)
    const auto r = mbedtls_ssl_tls_prf(mTlsPrfType,
                                       mMasterSecret,
                                       sizeof(mMasterSecret),
                                       kSrtpExporterLabel,
                                       mClientServerRandom,
                                       sizeof(mClientServerRandom),
                                       material.data(),
                                       materialSize);
    if (r != 0) {
        return { std::nullopt, make_error("Cannot export SRTP keying material", r) };
    }

    const auto clientKey = material.data();
    const auto serverKey = clientKey + info->keySize;
    const auto clientSalt = serverKey + info->keySize;
    const auto serverSalt = clientSalt + info->saltSize;

    std::optional<SrtpKeys> keys;
    keys.emplace(info->id,
                 info->name,
                 mIsSetupActive,
                 clientKey,
                 serverKey,
                 clientSalt,
                 serverSalt,
                 info->keySize,
                 info->saltSize);

    // Don't leave the raw key material around
    secureZero(material.data(), materialSize);

    return { std::move(keys), Error::OK };
}

ByteBuffer DtlsSession::getRemoteCertificateHash(const std::string& alg) const
{
    if (mPeerCertificateDer.empty()) {
        return {};
    }

    const auto digest = get_digest(alg);
    if (digest == nullptr) {
        LOG(SRTC_LOG_E, "Unsupported certificate hash algorithm: %s", alg.c_str());
        return {};
    }

    uint8_t buf[MBEDTLS_MD_MAX_SIZE] = {};
    if (mbedtls_md(digest, mPeerCertificateDer.data(), mPeerCertificateDer.size(), buf) != 0) {
        return {};
    }

    return { buf, mbedtls_md_get_size(digest) };
}

DtlsState DtlsSession::getState() const
{
    return mState;
}

ProfileId DtlsSession::getConnectionProfile() const
{
    // The handshake doesn't complete without a supported profile
    std::optional<SrtpProfileInfo> info;
    if (mIsSslSetup) {
        mbedtls_dtls_srtp_info srtpInfo = {};
        mbedtls_ssl_get_dtls_srtp_negotiation_result(&mSsl, &srtpInfo);
        info = get_srtp_profile_info(srtpInfo.MBEDTLS_PRIVATE(chosen_dtls_srtp_profile));
    }
    assert(info);

    return info ? info->id : ProfileId::kAES_128_CM_SHA1_80;
}

std::string DtlsSession::getConnectionCipherName() const
{
    if (!mIsSslSetup) {
        return {};
    }

    const auto name = mbedtls_ssl_get_ciphersuite(&mSsl);
    return name ? name : "";
}

void DtlsSession::onHandshakeStep()
{
    const auto r = mbedtls_ssl_handshake(&mSsl);
    LOG(SRTC_LOG_V, "DTLS handshake: %d", r);

    if (r == 0) {
        // This may destroy us
        onHandshakeCompleted();
    } else if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
        // This includes MBEDTLS_ERR_SSL_TIMEOUT, when retransmits run out
        // This may destroy us
        onHandshakeFailed(make_error("Failure during DTLS handshake", r));
    }

    // Otherwise still in progress, mbedTLS manages the retransmit timer through timer_set()
}

void DtlsSession::onHandshakeCompleted()
{
    Task::cancelHelper(mTaskRetransmit);
    mIsTimerActive = false;

    // The peer has to have a certificate
    if (mPeerCertificateDer.empty()) {
        onHandshakeFailed({ Error::Code::InvalidData, "There is no DTLS peer certificate" });
        return;
    }

    // Which has to match the fingerprint from the SDP
    const auto actualHash = getRemoteCertificateHash(mExpectedHash.getAlg());
    LOG(SRTC_LOG_V,
        "Remote certificate %s: %s",
        mExpectedHash.getAlg().c_str(),
        bin_to_hex(actualHash.data(), actualHash.size()).c_str());

    if (actualHash.empty() || !(actualHash == mExpectedHash.getBin())) {
        onHandshakeFailed({ Error::Code::InvalidData, "Certificate hash doesn't match" });
        return;
    }

    // And we need SRTP
    mbedtls_dtls_srtp_info srtpInfo = {};
    mbedtls_ssl_get_dtls_srtp_negotiation_result(&mSsl, &srtpInfo);
    const auto info = get_srtp_profile_info(srtpInfo.MBEDTLS_PRIVATE(chosen_dtls_srtp_profile));
    if (!info) {
        onHandshakeFailed({ Error::Code::InvalidData, "No supported SRTP profile was negotiated" });
        return;
    }

    if (!mHasMasterSecret) {
        onHandshakeFailed({ Error::Code::InvalidData, "The DTLS master secret was not exported" });
        return;
    }

    mState = DtlsState::Completed;

    LOG(SRTC_LOG_V,
        "DTLS connected as %s with cipher %s, profile %s, ice rtt = %.2f ms",
        mIsSetupActive ? "server" : "client",
        getConnectionCipherName().c_str(),
        info->name,
        mIceRttMillis);

    // This may destroy us, so it has to be the last thing we do. Copy the callback first,
    // so it's not destroyed while it's running
    const auto fn = mConnectSuccessFn;
    fn();
}

void DtlsSession::onHandshakeFailed(const Error& error)
{
    LOG(SRTC_LOG_E, "DTLS handshake failed: %s", error.message.c_str());

    mState = DtlsState::Failed;
    freeSsl();

    // This may destroy us, so it has to be the last thing we do. Copy the callback first,
    // so it's not destroyed while it's running
    const auto fn = mConnectFailedFn;
    fn(error);
}

// Retransmits

void DtlsSession::onRetransmitTimeout()
{
    if (!mIsSslSetup || mState != DtlsState::Activating) {
        return;
    }

    // timer_get() now returns 2, so the handshake retransmits the last flight, or fails if it ran out of time
    LOG(SRTC_LOG_V, "DTLS retransmit timeout");

    // This may destroy us
    onHandshakeStep();
}

void DtlsSession::timer_set(void* ctx, uint32_t int_ms, uint32_t fin_ms)
{
    const auto session = static_cast<DtlsSession*>(ctx);

    Task::cancelHelper(session->mTaskRetransmit);

    if (fin_ms == 0) {
        session->mIsTimerActive = false;
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    session->mIsTimerActive = true;
    session->mTimerIntermediate = now + std::chrono::milliseconds(int_ms);
    session->mTimerFinal = now + std::chrono::milliseconds(fin_ms);

    LOG(SRTC_LOG_V, "DTLS retransmit timer in %u ms", static_cast<unsigned int>(fin_ms));

    session->mTaskRetransmit = session->mScheduler.submit(
        std::chrono::milliseconds(fin_ms), __FILE__, __LINE__, [session] { session->onRetransmitTimeout(); });
}

int DtlsSession::timer_get(void* ctx)
{
    const auto session = static_cast<DtlsSession*>(ctx);

    if (!session->mIsTimerActive) {
        return -1;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= session->mTimerFinal) {
        return 2;
    }
    if (now >= session->mTimerIntermediate) {
        return 1;
    }
    return 0;
}

// I/O

int DtlsSession::bio_send(void* ctx, const unsigned char* buf, size_t len)
{
    const auto session = static_cast<DtlsSession*>(ctx);
    session->mSendFn(buf, len);
    return static_cast<int>(len);
}

int DtlsSession::bio_recv(void* ctx, unsigned char* buf, size_t len)
{
    const auto session = static_cast<DtlsSession*>(ctx);
    if (session->mReceiveQueue.empty()) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }

    // One datagram per call
    const auto item = std::move(session->mReceiveQueue.front());
    session->mReceiveQueue.pop_front();

    const auto size = std::min(item.size(), len);
    std::memcpy(buf, item.data(), size);

    return static_cast<int>(size);
}

// Certificate verification and key export

int DtlsSession::verify_cb(void* ctx, mbedtls_x509_crt* crt, int depth, uint32_t* flags)
{
    const auto session = static_cast<DtlsSession*>(ctx);

    if (depth == 0) {
        // The peer's own certificate, we check its fingerprint once the handshake completes
        session->mPeerCertificateDer.assign(crt->raw.p, crt->raw.len);
    }

    // WebRTC certificates are self signed, trust comes from the fingerprint in the SDP
    *flags = 0;
    return 0;
}

void DtlsSession::export_keys_cb(void* ctx,
                                 mbedtls_ssl_key_export_type type,
                                 const unsigned char* secret,
                                 size_t secret_len,
                                 const unsigned char client_random[32],
                                 const unsigned char server_random[32],
                                 mbedtls_tls_prf_types tls_prf_type)
{
    const auto session = static_cast<DtlsSession*>(ctx);

    if (type != MBEDTLS_SSL_KEY_EXPORT_TLS12_MASTER_SECRET || secret_len != sizeof(session->mMasterSecret)) {
        return;
    }

    std::memcpy(session->mMasterSecret, secret, secret_len);
    std::memcpy(session->mClientServerRandom, client_random, 32);
    std::memcpy(session->mClientServerRandom + 32, server_random, 32);
    session->mTlsPrfType = tls_prf_type;
    session->mHasMasterSecret = true;
}

} // namespace srtc::compat
