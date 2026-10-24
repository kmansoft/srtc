#pragma once

// Include via crypto_dtls_session.h, which defines DtlsState

#include "srtc/byte_buffer.h"
#include "srtc/error.h"
#include "srtc/scheduler.h"
#include "srtc/srtp_keys.h"
#include "srtc/srtp_util.h"
#include "srtc/x509_hash.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ssl.h>

#include <chrono>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>

#include <cstdint>

namespace srtc
{
class X509Certificate;
}

namespace srtc::compat
{

class DtlsSession
{
public:
    // Outgoing DTLS packets, called from within our methods
    using SendFn = std::function<void(const uint8_t* data, size_t size)>;
    // These may destroy the session, we will not touch any memory after calling them
    using ConnectSuccessFn = std::function<void()>;
    using ConnectFailedFn = std::function<void(const Error&)>;

    DtlsSession(const DtlsSession&) = delete;
    DtlsSession(DtlsSession&&) = delete;

    DtlsSession& operator=(const DtlsSession&) = delete;
    DtlsSession& operator=(DtlsSession&&) = delete;

    // isSetupActive: the remote's answer has a=setup:active, so we are the DTLS server
    // expectedHash: the remote certificate fingerprint from the SDP answer, checked before reporting success
    // iceRttMillis: used for the initial retransmit timeout, can be 0 if unknown
    static std::pair<std::shared_ptr<DtlsSession>, Error> create(bool isSetupActive,
                                                                 const std::shared_ptr<X509Certificate>& certificate,
                                                                 const X509Hash& expectedHash,
                                                                 const std::shared_ptr<RealScheduler>& scheduler,
                                                                 float iceRttMillis,
                                                                 const SendFn& sendFn,
                                                                 const ConnectSuccessFn& connectSuccessFn,
                                                                 const ConnectFailedFn& connectFailedFn);

    ~DtlsSession();

    // Sets up mbedTLS and starts the handshake, as a client this sends the ClientHello.
    // On error, the state is Failed
    [[nodiscard]] Error start();

    // Shutting down, sends close_notify if connected
    void close();

    // Receiving data
    void received(ByteBuffer&& buf);

    // Sending application data (SCTP)
    [[nodiscard]] Error send(const ByteBuffer& buf);

    // Once the handshake completes
    [[nodiscard]] std::pair<std::optional<SrtpKeys>, Error> getSrtpKeys() const;
    // alg is from the SDP fingerprint attribute, e.g. "sha-256"
    [[nodiscard]] ByteBuffer getRemoteCertificateHash(const std::string& alg) const;

    [[nodiscard]] DtlsState getState() const;

    [[nodiscard]] ProfileId getConnectionProfile() const;
    [[nodiscard]] std::string getConnectionCipherName() const;

    // Reading application data, call after received() until no data and no EOF / error
    // Success: buf.size() > 0
    // No more data: buf.size() == 0, bool is false, no error
    // EOF (close_notify from peer): bool is true
    // Error: in pair.second
    [[nodiscard]] std::pair<bool, Error> read(ByteBuffer& buf);

    // Implementation
    DtlsSession(bool isSetupActive,
                const std::shared_ptr<X509Certificate>& certificate,
                const X509Hash& expectedHash,
                const std::shared_ptr<RealScheduler>& scheduler,
                float iceRttMillis,
                const SendFn& sendFn,
                const ConnectSuccessFn& connectSuccessFn,
                const ConnectFailedFn& connectFailedFn);

private:
    const bool mIsSetupActive;
    const std::shared_ptr<X509Certificate> mCertificate;
    const X509Hash mExpectedHash;
    const float mIceRttMillis;
    const SendFn mSendFn;
    const ConnectSuccessFn mConnectSuccessFn;
    const ConnectFailedFn mConnectFailedFn;

    DtlsState mState;

    // mbedTLS, mIsSslSetup tells if mSsl / mConf are set up and need to be freed
    mbedtls_entropy_context mEntropy;
    mbedtls_ctr_drbg_context mDrbg;
    mbedtls_ssl_config mConf;
    mbedtls_ssl_context mSsl;
    bool mIsSslSetup;

    std::list<ByteBuffer> mReceiveQueue;

    // The peer's certificate, captured during verification
    ByteBuffer mPeerCertificateDer;

    // Captured during the handshake, used to export the SRTP keying material
    bool mHasMasterSecret;
    unsigned char mMasterSecret[48];
    unsigned char mClientServerRandom[64];
    mbedtls_tls_prf_types mTlsPrfType;

    [[nodiscard]] Error init();
    void freeSsl();

    void onHandshakeStep();
    void onHandshakeCompleted();
    void onHandshakeFailed(const Error& error);

    // Retransmits, mbedTLS sets the timer and we call the handshake again when it expires
    std::weak_ptr<Task> mTaskRetransmit;
    bool mIsTimerActive;
    std::chrono::steady_clock::time_point mTimerIntermediate;
    std::chrono::steady_clock::time_point mTimerFinal;

    void onRetransmitTimeout();

    static void timer_set(void* ctx, uint32_t int_ms, uint32_t fin_ms);
    static int timer_get(void* ctx);

    // I/O
    static int bio_send(void* ctx, const unsigned char* buf, size_t len);
    static int bio_recv(void* ctx, unsigned char* buf, size_t len);

    // Certificate verification and key export
    static int verify_cb(void* ctx, mbedtls_x509_crt* crt, int depth, uint32_t* flags);
    static void export_keys_cb(void* ctx,
                               mbedtls_ssl_key_export_type type,
                               const unsigned char* secret,
                               size_t secret_len,
                               const unsigned char client_random[32],
                               const unsigned char server_random[32],
                               mbedtls_tls_prf_types tls_prf_type);

    // Has to be last
    ScopedScheduler mScheduler;
};

} // namespace srtc::compat
