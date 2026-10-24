#ifdef _WIN32
#include "srtc/srtc.h"
#include <wincrypt.h>
#undef X509_NAME
#undef X509_EXTENSIONS
#undef PKCS7_SIGNER_INFO
#endif

#include "crypto_dtls_session.h"

#include "srtc/logging.h"
#include "srtc/util.h"
#include "srtc/x509_certificate.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cstring>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/srtp.h>
#include <openssl/x509.h>

#define LOG(level, ...) srtc::log(level, "DtlsSession", __VA_ARGS__)

namespace
{

const char* const kSrtpProfileList =
    "SRTP_AEAD_AES_128_GCM:SRTP_AEAD_AES_256_GCM:SRTP_AES128_CM_SHA1_80:SRTP_AES128_CM_SHA1_32";

const char* const kSrtpExporterLabel = "EXTRACTOR-dtls_srtp";

constexpr long kMTU = 1200;

// Max DTLS record plaintext size
constexpr size_t kMaxReadSize = 16384;

constexpr auto kMinRetransmitTimeout = std::chrono::milliseconds(50);
constexpr auto kMaxInitialRetransmitTimeout = std::chrono::milliseconds(1000);
constexpr auto kMaxRetransmitTimeout = std::chrono::milliseconds(2000);

struct SrtpProfileInfo {
    srtc::ProfileId id;
    const char* name;
    size_t keySize;
    size_t saltSize;
};

std::optional<SrtpProfileInfo> get_srtp_profile_info(unsigned long sslProfileId)
{
    switch (sslProfileId) {
    case SRTP_AEAD_AES_256_GCM:
        return SrtpProfileInfo{ srtc::ProfileId::kAES_256_GCM, "AES_256_GCM", 32, 12 };
    case SRTP_AEAD_AES_128_GCM:
        return SrtpProfileInfo{ srtc::ProfileId::kAES_128_GCM, "AES_128_GCM", 16, 12 };
    case SRTP_AES128_CM_SHA1_80:
        return SrtpProfileInfo{ srtc::ProfileId::kAES_128_CM_SHA1_80, "AES_128_CM_SHA1_80", 16, 14 };
    case SRTP_AES128_CM_SHA1_32:
        return SrtpProfileInfo{ srtc::ProfileId::kAES_128_CM_SHA1_32, "AES_128_CM_SHA1_32", 16, 14 };
    default:
        return std::nullopt;
    }
}

const EVP_MD* get_digest(const std::string& alg)
{
    std::string lower = alg;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });

    if (lower == "sha-256" || lower == "sha256") {
        return EVP_sha256();
    } else if (lower == "sha-384" || lower == "sha384") {
        return EVP_sha384();
    } else if (lower == "sha-512" || lower == "sha512") {
        return EVP_sha512();
    } else if (lower == "sha-1" || lower == "sha1") {
        return EVP_sha1();
    }

    return nullptr;
}

int verify_callback([[maybe_unused]] int ok, [[maybe_unused]] X509_STORE_CTX* store_ctx)
{
    // WebRTC certificates are self signed, the caller checks the fingerprint against the SDP
    return 1;
}

std::string get_openssl_error()
{
    BIO* bio = BIO_new(BIO_s_mem());
    if (bio == nullptr) {
        return {};
    }

    ERR_print_errors(bio);
    char* buf = nullptr;
    const auto len = BIO_get_mem_data(bio, &buf);
    std::string ret(buf, static_cast<size_t>(len));
    BIO_free(bio);
    return ret;
}

} // namespace

namespace srtc::compat
{

std::once_flag DtlsSession::gBioOnce;
BIO_METHOD* DtlsSession::gBioMethod = nullptr;

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
    , mCtx(nullptr)
    , mSsl(nullptr)
    , mPeerCertificate(nullptr)
    , mScheduler(scheduler)
{
}

DtlsSession::~DtlsSession()
{
    // No close_notify here, the owner may be partially destroyed, use close() for that
    freeSsl();
}

Error DtlsSession::init()
{
    // We are the server when the remote is active
    mCtx = SSL_CTX_new(mIsSetupActive ? DTLS_server_method() : DTLS_client_method());
    if (mCtx == nullptr) {
        return { Error::Code::InvalidData, "Cannot create the DTLS context: " + get_openssl_error() };
    }

    if (SSL_CTX_use_certificate(mCtx, mCertificate->getCertificate()) != 1 ||
        SSL_CTX_use_PrivateKey(mCtx, mCertificate->getPrivateKey()) != 1 || SSL_CTX_check_private_key(mCtx) != 1) {
        return { Error::Code::InvalidData, "Invalid certificate or private key: " + get_openssl_error() };
    }

    // Makes the server request the client certificate, so we can check its fingerprint
    SSL_CTX_set_verify(mCtx, SSL_VERIFY_PEER, verify_callback);

    SSL_CTX_set_min_proto_version(mCtx, DTLS1_VERSION);
    SSL_CTX_set_max_proto_version(mCtx, DTLS1_2_VERSION);
    SSL_CTX_set_read_ahead(mCtx, 1);

    mSsl = SSL_new(mCtx);
    if (mSsl == nullptr) {
        return { Error::Code::InvalidData, "Cannot create the DTLS session: " + get_openssl_error() };
    }

    std::call_once(gBioOnce, [] {
        gBioMethod = BIO_meth_new(BIO_TYPE_DGRAM, "srtc_dtls");
        BIO_meth_set_read(gBioMethod, bio_read);
        BIO_meth_set_write(gBioMethod, bio_write);
        BIO_meth_set_ctrl(gBioMethod, bio_ctrl);
    });

    const auto bio = BIO_new(gBioMethod);
    if (bio == nullptr) {
        return { Error::Code::InvalidData, "Cannot create the DTLS BIO: " + get_openssl_error() };
    }
    BIO_set_data(bio, this);
    BIO_set_init(bio, 1);

    // The session takes ownership of the BIO
    SSL_set_bio(mSsl, bio, bio);

    // Note that this returns 0 on success
    if (SSL_set_tlsext_use_srtp(mSsl, kSrtpProfileList) != 0) {
        return { Error::Code::InvalidData, "Cannot set SRTP profiles: " + get_openssl_error() };
    }

    SSL_set_app_data(mSsl, this);
    DTLS_set_timer_cb(mSsl, timer_cb);

    if (mIsSetupActive) {
        SSL_set_accept_state(mSsl);
    } else {
        SSL_set_connect_state(mSsl);
    }

    return Error::OK;
}

void DtlsSession::freeSsl()
{
    Task::cancelHelper(mTaskRetransmit);

    if (mPeerCertificate) {
        X509_free(mPeerCertificate);
        mPeerCertificate = nullptr;
    }

    if (mSsl) {
        // Also frees the BIO
        SSL_free(mSsl);
        mSsl = nullptr;
    }

    if (mCtx) {
        SSL_CTX_free(mCtx);
        mCtx = nullptr;
    }

    mReceiveQueue.clear();
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
    const auto r = SSL_do_handshake(mSsl);
    const auto err = SSL_get_error(mSsl, r);
    if (err != SSL_ERROR_WANT_READ) {
        const auto opensslError = get_openssl_error();
        mState = DtlsState::Failed;
        freeSsl();
        return { Error::Code::InvalidData, "Cannot start the DTLS handshake: " + opensslError };
    }

    scheduleRetransmit();
    return Error::OK;
}

void DtlsSession::close()
{
    if (mSsl && mState == DtlsState::Completed) {
        // Sends close_notify through our BIO
        SSL_shutdown(mSsl);
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
    if (mState != DtlsState::Completed || mSsl == nullptr) {
        return { Error::Code::InvalidData, "The DTLS session is not connected" };
    }

    const auto r = SSL_write(mSsl, buf.data(), static_cast<int>(buf.size()));
    if (r <= 0) {
        const auto err = SSL_get_error(mSsl, r);
        return { Error::Code::InvalidData, "SSL_write failed, ssl error = " + std::to_string(err) };
    }

    return Error::OK;
}

std::pair<bool, Error> DtlsSession::read(ByteBuffer& buf)
{
    buf.clear();

    if (mState != DtlsState::Completed || mSsl == nullptr) {
        return { false, { Error::Code::InvalidData, "The DTLS session is not connected" } };
    }

    buf.reserve(kMaxReadSize);

    // This also lets OpenSSL respond to retransmitted handshake flights from the peer
    const auto r = SSL_read(mSsl, buf.data(), static_cast<int>(kMaxReadSize));
    if (r > 0) {
        buf.resize(static_cast<size_t>(r));
        return { false, Error::OK };
    }

    if ((SSL_get_shutdown(mSsl) & SSL_RECEIVED_SHUTDOWN) != 0) {
        LOG(SRTC_LOG_V, "Received DTLS close_notify, peer disconnected gracefully");
        mState = DtlsState::Closed;
        return { true, Error::OK };
    }

    const auto err = SSL_get_error(mSsl, r);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
        // No more data for now
        return { false, Error::OK };
    }

    LOG(SRTC_LOG_V, "DTLS connection lost, ssl error = %d", err);
    mState = DtlsState::Closed;
    return { false, { Error::Code::InvalidData, "DTLS connection lost unexpectedly" } };
}

std::pair<std::optional<SrtpKeys>, Error> DtlsSession::getSrtpKeys() const
{
    if (mState != DtlsState::Completed || mSsl == nullptr) {
        return { std::nullopt, { Error::Code::InvalidData, "The DTLS session is not connected" } };
    }

    // https://datatracker.ietf.org/doc/html/rfc5764#section-4.2
    const auto profile = SSL_get_selected_srtp_profile(mSsl);
    if (profile == nullptr) {
        return { std::nullopt, { Error::Code::InvalidData, "No SRTP profile was negotiated" } };
    }

    const auto info = get_srtp_profile_info(profile->id);
    if (!info) {
        return { std::nullopt, { Error::Code::InvalidData, "Unsupported SRTP profile" } };
    }

    // client key, server key, client salt, server salt
    const auto materialSize = (info->keySize + info->saltSize) * 2;
    ByteBuffer material(materialSize);

    if (SSL_export_keying_material(mSsl,
                                   material.data(),
                                   materialSize,
                                   kSrtpExporterLabel,
                                   std::strlen(kSrtpExporterLabel),
                                   nullptr,
                                   0,
                                   0) != 1) {
        return { std::nullopt, { Error::Code::InvalidData, "Cannot export SRTP keying material: " + get_openssl_error() } };
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
    std::memset(material.data(), 0, materialSize);

    return { std::move(keys), Error::OK };
}

ByteBuffer DtlsSession::getRemoteCertificateHash(const std::string& alg) const
{
    if (mPeerCertificate == nullptr) {
        return {};
    }

    const auto digest = get_digest(alg);
    if (digest == nullptr) {
        LOG(SRTC_LOG_E, "Unsupported certificate hash algorithm: %s", alg.c_str());
        return {};
    }

    uint8_t buf[EVP_MAX_MD_SIZE] = {};
    unsigned int size = 0;

    if (X509_digest(mPeerCertificate, digest, buf, &size) != 1) {
        return {};
    }

    return { buf, size };
}

DtlsState DtlsSession::getState() const
{
    return mState;
}

ProfileId DtlsSession::getConnectionProfile() const
{
    // The handshake doesn't complete without a supported profile
    const auto profile = mSsl ? SSL_get_selected_srtp_profile(mSsl) : nullptr;
    const auto info = profile ? get_srtp_profile_info(profile->id) : std::nullopt;
    assert(info);

    return info ? info->id : ProfileId::kAES_128_CM_SHA1_80;
}

std::string DtlsSession::getConnectionCipherName() const
{
    if (mSsl == nullptr) {
        return {};
    }

    const auto name = SSL_get_cipher(mSsl);
    return name ? name : "???";
}

void DtlsSession::onHandshakeStep()
{
    const auto r = SSL_do_handshake(mSsl);
    const auto err = SSL_get_error(mSsl, r);
    LOG(SRTC_LOG_V, "DTLS handshake: %d, %d", r, err);

    if (r == 1) {
        // This may destroy us
        onHandshakeCompleted();
    } else if (err == SSL_ERROR_WANT_READ) {
        // Still in progress, a new flight may have been sent
        scheduleRetransmit();
    } else {
        // This may destroy us
        onHandshakeFailed({ Error::Code::InvalidData, "Failure during DTLS handshake: " + get_openssl_error() });
    }
}

void DtlsSession::onHandshakeCompleted()
{
    Task::cancelHelper(mTaskRetransmit);

    // The peer has to have a certificate, its fingerprint is checked by the caller.
    // Both of these return a new reference, which we keep until freeSsl()
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    mPeerCertificate = SSL_get1_peer_certificate(mSsl);
#else
    mPeerCertificate = SSL_get_peer_certificate(mSsl);
#endif
    if (mPeerCertificate == nullptr) {
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
    const auto profile = SSL_get_selected_srtp_profile(mSsl);
    if (profile == nullptr || !get_srtp_profile_info(profile->id)) {
        onHandshakeFailed({ Error::Code::InvalidData, "No supported SRTP profile was negotiated" });
        return;
    }

    mState = DtlsState::Completed;

    LOG(SRTC_LOG_V,
        "DTLS connected as %s with cipher %s, profile %s, ice rtt = %.2f ms",
        mIsSetupActive ? "server" : "client",
        SSL_get_cipher(mSsl),
        profile->name,
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

void DtlsSession::scheduleRetransmit()
{
    Task::cancelHelper(mTaskRetransmit);

    if (mSsl == nullptr || mState != DtlsState::Activating) {
        return;
    }

    // OpenSSL tracks the retransmit deadline but does not fire it on its own, so we drive it from our scheduler
    timeval tv = {};
    if (DTLSv1_get_timeout(mSsl, &tv) != 1) {
        // No timer armed, e.g. we are the server still waiting for the ClientHello
        return;
    }

    const auto delay =
        std::chrono::ceil<Scheduler::Delay>(std::chrono::seconds(tv.tv_sec) + std::chrono::microseconds(tv.tv_usec));
    LOG(SRTC_LOG_V, "DTLS retransmit timer in %lld ms", static_cast<long long>(delay.count()));

    mTaskRetransmit = mScheduler.submit(delay, __FILE__, __LINE__, [this] { onRetransmitTimeout(); });
}

void DtlsSession::onRetransmitTimeout()
{
    if (mSsl == nullptr || mState != DtlsState::Activating) {
        return;
    }

    // Retransmits the last flight through our BIO
    const auto r = DTLSv1_handle_timeout(mSsl);
    if (r < 0) {
        // This may destroy us
        onHandshakeFailed({ Error::Code::InvalidData, "DTLS handshake retransmit failed: " + get_openssl_error() });
        return;
    }

    LOG(SRTC_LOG_V, "DTLS retransmit timeout handled: %d", static_cast<int>(r));
    scheduleRetransmit();
}

unsigned int DtlsSession::timer_cb(SSL* ssl, unsigned int timer_us)
{
    using namespace std::chrono;

    microseconds timeout;

    if (timer_us == 0) {
        // Initial timeout for a new flight: twice the ICE round trip time
        const auto session = static_cast<DtlsSession*>(SSL_get_app_data(ssl));
        const auto iceRttMs = session ? session->mIceRttMillis : 0.0f;
        const auto iceRtt = duration_cast<microseconds>(duration<float, std::milli>(iceRttMs));
        timeout = std::clamp<microseconds>(2 * iceRtt, kMinRetransmitTimeout, kMaxInitialRetransmitTimeout);
    } else {
        // Exponential backoff for subsequent retransmits of the same flight
        timeout = std::min<microseconds>(2 * microseconds(timer_us), kMaxRetransmitTimeout);
    }

    LOG(SRTC_LOG_V,
        "DTLS retransmit timeout: previous = %u us, next = %lld us",
        timer_us,
        static_cast<long long>(timeout.count()));

    return static_cast<unsigned int>(timeout.count());
}

// OpenSSL BIO

int DtlsSession::bio_read(BIO* b, char* out, int outl)
{
    if (out == nullptr || outl <= 0) {
        return 0;
    }

    BIO_clear_retry_flags(b);

    const auto session = static_cast<DtlsSession*>(BIO_get_data(b));
    if (session->mReceiveQueue.empty()) {
        BIO_set_retry_read(b);
        return -1;
    }

    const auto item = std::move(session->mReceiveQueue.front());
    session->mReceiveQueue.pop_front();

    const auto ret = std::min(static_cast<int>(item.size()), outl);
    std::memcpy(out, item.data(), static_cast<size_t>(ret));

    return ret;
}

int DtlsSession::bio_write(BIO* b, const char* in, int inl)
{
    if (inl <= 0) {
        return 0;
    }

    const auto session = static_cast<DtlsSession*>(BIO_get_data(b));
    session->mSendFn(reinterpret_cast<const uint8_t*>(in), static_cast<size_t>(inl));

    return inl;
}

long DtlsSession::bio_ctrl([[maybe_unused]] BIO* b, int cmd, [[maybe_unused]] long num, [[maybe_unused]] void* ptr)
{
    switch (cmd) {
    case BIO_CTRL_DGRAM_QUERY_MTU:
        return kMTU;
    case BIO_CTRL_DUP:
    case BIO_CTRL_FLUSH:
        return 1;
    default:
        return 0;
    }
}

} // namespace srtc::compat
