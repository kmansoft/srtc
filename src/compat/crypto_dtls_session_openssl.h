#pragma once

#include "srtc/srtp_util.h"
#include "srtc/error.h"
#include "srtc/srtp_keys.h"
#include "srtc/byte_buffer.h"
#include "srtc/scheduler.h"

#include <functional>
#include <memory>
#include <queue>
#include <string>

#include <cstdint>

namespace srtc::compat
{

class X509Certificate;
class RealScheduler;

class DtlsSession
{
public:
    using SendFn = std::function<void(const uint8_t* data, size_t size)>;
    using ConnectSuccessFn = std::function<void()>;
    using ConnectFailedFn = std::function<void(const Error&)>;
    using DisconnectFn = std::function<void(const Error&)>;

    DtlsSession(const DtlsSession&) = delete;
    DtlsSession(DtlsSession&&) = delete;

    DtlsSession& operator=(const DtlsSession&) = delete;
    DtlsSession& operator=(DtlsSession&&) = delete;

    static std::pair<std::shared_ptr<DtlsSession>, Error> create(bool isSetupActive,
                                                                 const std::shared_ptr<X509Certificate>& certificate,
                                                                 const std::shared_ptr<RealScheduler>& scheduler,
                                                                 const SendFn& sendFn,
                                                                 const ConnectSuccessFn& connectSuccessFn,
                                                                 const ConnectFailedFn& connectFailedFn,
                                                                 const DisconnectFn& disconnectFn);

    // Starting the handshake
    [[nodiscard]] Error start();

    // Shutting down
    void close();

    // Receiving data
    void received(ByteBuffer&& buf);

    // Sending raw data
    [[nodiscard]] Error send(ByteBuffer&& buf);

    // Once the handshake completes
    [[nodiscard]] std::pair<SrtpKeys, Error> getSrtpKeys() const;
    [[nodiscard]] ByteBuffer getRemoteCertificateHash(const std::string& alg) const;

    [[nodiscard]] ProfileId getConnectionProfile() const;
    [[nodiscard]] std::string getConnectionCipherName() const;

    // Reading:
    // Success: buf.size() > 0
    // EOF: bool is true
    // Error: in pair.second
    [[nodiscard]] std::pair<bool, Error> read(ByteBuffer& buf) const;

    // Implementation
    DtlsSession(bool isSetupActive,
                const std::shared_ptr<X509Certificate>& certificate,
                const SendFn& sendFn,
                const ConnectSuccessFn& connectSuccessFn,
                const ConnectFailedFn& connectFailedFn,
                const DisconnectFn& disconnectFn);

private:
    const bool mIsSetupActive;
    const std::shared_ptr<X509Certificate> mCertificate;
    const SendFn mSendFn;
    const ConnectSuccessFn mConnectSuccessFn;
    const ConnectFailedFn mConnectFailedFn;
    const DisconnectFn mDisconnectFn;

    std::queue<ByteBuffer> mReceiveQueue;

    // Has to be last
    ScopedScheduler mScheduler;
};

} // namespace srtc
