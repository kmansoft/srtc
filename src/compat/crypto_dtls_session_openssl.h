#pragma once

// Include via crypto_dtls_session.h, which defines DtlsState

#include "srtc/byte_buffer.h"
#include "srtc/error.h"
#include "srtc/scheduler.h"
#include "srtc/srtp_keys.h"
#include "srtc/srtp_util.h"

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <cstdint>

struct ssl_ctx_st;
struct ssl_st;
struct x509_st;
struct bio_st;
struct bio_method_st;

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
    // iceRttMillis: used for the initial retransmit timeout, can be 0 if unknown
    static std::pair<std::shared_ptr<DtlsSession>, Error> create(bool isSetupActive,
                                                                 const std::shared_ptr<X509Certificate>& certificate,
                                                                 const std::shared_ptr<RealScheduler>& scheduler,
                                                                 float iceRttMillis,
                                                                 const SendFn& sendFn,
                                                                 const ConnectSuccessFn& connectSuccessFn,
                                                                 const ConnectFailedFn& connectFailedFn);

    ~DtlsSession();

    // Starting the handshake, as a client this sends the ClientHello
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
                const std::shared_ptr<RealScheduler>& scheduler,
                float iceRttMillis,
                const SendFn& sendFn,
                const ConnectSuccessFn& connectSuccessFn,
                const ConnectFailedFn& connectFailedFn);

private:
    const bool mIsSetupActive;
    const std::shared_ptr<X509Certificate> mCertificate;
    const float mIceRttMillis;
    const SendFn mSendFn;
    const ConnectSuccessFn mConnectSuccessFn;
    const ConnectFailedFn mConnectFailedFn;

    DtlsState mState;

    struct ssl_ctx_st* mCtx;
    struct ssl_st* mSsl;

    // The peer's certificate, we hold a reference once the handshake completes
    struct x509_st* mPeerCertificate;

    std::list<ByteBuffer> mReceiveQueue;

    [[nodiscard]] Error init();
    void freeSsl();

    void onHandshakeStep();
    void onHandshakeCompleted();
    void onHandshakeFailed(const Error& error);

    // Retransmits
    std::weak_ptr<Task> mTaskRetransmit;
    void scheduleRetransmit();
    void onRetransmitTimeout();

    static unsigned int timer_cb(struct ssl_st* ssl, unsigned int timer_us);

    // OpenSSL BIO
    static int bio_read(struct bio_st* b, char* out, int outl);
    static int bio_write(struct bio_st* b, const char* in, int inl);
    static long bio_ctrl(struct bio_st* b, int cmd, long num, void* ptr);

    static std::once_flag gBioOnce;
    static struct bio_method_st* gBioMethod;

    // Has to be last
    ScopedScheduler mScheduler;
};

} // namespace srtc::compat
