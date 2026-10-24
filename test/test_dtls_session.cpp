#include <gtest/gtest.h>

#include "srtc/byte_buffer.h"
#include "srtc/error.h"
#include "srtc/scheduler.h"
#include "srtc/srtp_keys.h"
#include "srtc/srtp_util.h"
#include "srtc/x509_certificate.h"
#include "srtc/x509_hash.h"

#include "crypto_dtls_session.h"
#include "crypto_provider.h"

#include <chrono>
#include <cstring>
#include <list>
#include <memory>
#include <string>
#include <thread>

// Two DTLS sessions talking to each other in memory, one as the client and one as the server.
// Uses only the compat interface, so this covers every crypto backend.

using srtc::compat::DtlsSession;
using srtc::compat::DtlsState;

namespace
{

constexpr auto kPumpTimeout = std::chrono::seconds(5);

srtc::X509Hash make_hash(const std::shared_ptr<srtc::X509Certificate>& certificate)
{
    return { "sha-256", certificate->getSha256FingerprintBin(), certificate->getSha256FingerprintHex() };
}

srtc::X509Hash make_wrong_hash()
{
    uint8_t bytes[32];
    std::memset(bytes, 0xAB, sizeof(bytes));
    return { "sha-256", srtc::ByteBuffer(bytes, sizeof(bytes)), "" };
}

bool equals(const srtc::CryptoBytes& a, const srtc::CryptoBytes& b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()) == 0;
}

struct Peer {
    std::shared_ptr<srtc::X509Certificate> certificate = std::make_shared<srtc::X509Certificate>();
    std::shared_ptr<DtlsSession> session;

    // Packets sent by this peer, not yet delivered to the other one
    std::list<srtc::ByteBuffer> outgoing;
    // To simulate packet loss, drop this many of the first packets we send
    size_t dropFirstCount = 0;
    size_t sentCount = 0;

    bool connected = false;
    bool failed = false;
    std::string failureMessage;

    std::list<std::string> received;
    bool eof = false;
    std::string readError;
};

} // namespace

class DtlsSessionTest : public ::testing::Test
{
protected:
    void SetUp() override { srtc::compat::initCryptoProvider(); }

    // Declared first, so it's destroyed after the sessions
    const std::shared_ptr<srtc::LoopScheduler> mScheduler = std::make_shared<srtc::LoopScheduler>();

    Peer mClient;
    Peer mServer;

    void createSessions(const srtc::X509Hash& clientExpectedHash, const srtc::X509Hash& serverExpectedHash)
    {
        // isSetupActive means the remote is active, so we are the server
        mClient.session = createSession(mClient, false, clientExpectedHash);
        mServer.session = createSession(mServer, true, serverExpectedHash);

        ASSERT_TRUE(mClient.session);
        ASSERT_TRUE(mServer.session);
    }

    void createSessions() { createSessions(make_hash(mServer.certificate), make_hash(mClient.certificate)); }

    void start()
    {
        ASSERT_TRUE(mClient.session && mServer.session);

        // The server waits for the ClientHello, the client sends it
        ASSERT_TRUE(mServer.session->start().isOk());
        ASSERT_TRUE(mClient.session->start().isOk());
    }

    void connect()
    {
        ASSERT_NO_FATAL_FAILURE(createSessions());
        ASSERT_NO_FATAL_FAILURE(start());

        ASSERT_TRUE(pump([this] { return mClient.connected && mServer.connected; }));
        ASSERT_FALSE(mClient.failed) << mClient.failureMessage;
        ASSERT_FALSE(mServer.failed) << mServer.failureMessage;
    }

    std::shared_ptr<DtlsSession> createSession(Peer& peer, bool isServer, const srtc::X509Hash& expectedHash)
    {
        const auto p = &peer;

        const auto [session, error] = DtlsSession::create(
            isServer,
            peer.certificate,
            expectedHash,
            mScheduler,
            0.0f,
            [p](const uint8_t* data, size_t size) {
                p->sentCount += 1;
                if (p->sentCount > p->dropFirstCount) {
                    p->outgoing.emplace_back(data, size);
                }
            },
            [p]() { p->connected = true; },
            [p](const srtc::Error& failure) {
                p->failed = true;
                p->failureMessage = failure.message;
            });

        EXPECT_TRUE(error.isOk()) << error.message;
        return session;
    }

    // Delivers packets between the peers and runs the retransmit timers, until done() or timeout
    template <typename Done>
    bool pump(Done done)
    {
        const auto deadline = std::chrono::steady_clock::now() + kPumpTimeout;

        while (std::chrono::steady_clock::now() < deadline) {
            const auto movedToServer = deliver(mClient, mServer);
            const auto movedToClient = deliver(mServer, mClient);

            mScheduler->run();

            if (done()) {
                return true;
            }

            if (!movedToServer && !movedToClient) {
                // Waiting for a retransmit timer
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        return done();
    }

    static bool deliver(Peer& from, Peer& to)
    {
        bool moved = false;

        while (!from.outgoing.empty()) {
            auto buf = std::move(from.outgoing.front());
            from.outgoing.pop_front();
            moved = true;

            if (!to.session) {
                continue;
            }

            to.session->received(std::move(buf));
            readAll(to);
        }

        return moved;
    }

    static void readAll(Peer& peer)
    {
        srtc::ByteBuffer buf;

        while (peer.session->getState() == DtlsState::Completed) {
            const auto [eof, error] = peer.session->read(buf);
            if (error.isError()) {
                peer.readError = error.message;
                return;
            }
            if (eof) {
                peer.eof = true;
                return;
            }
            if (buf.empty()) {
                return;
            }

            peer.received.emplace_back(reinterpret_cast<const char*>(buf.data()), buf.size());
        }
    }

    static srtc::ByteBuffer toBuffer(const std::string& s)
    {
        return { reinterpret_cast<const uint8_t*>(s.data()), s.size() };
    }
};

TEST_F(DtlsSessionTest, Handshake)
{
    ASSERT_NO_FATAL_FAILURE(connect());

    EXPECT_EQ(mClient.session->getState(), DtlsState::Completed);
    EXPECT_EQ(mServer.session->getState(), DtlsState::Completed);

    EXPECT_EQ(mClient.session->getConnectionProfile(), mServer.session->getConnectionProfile());
    EXPECT_FALSE(mClient.session->getConnectionCipherName().empty());
    EXPECT_EQ(mClient.session->getConnectionCipherName(), mServer.session->getConnectionCipherName());

    // Each side sees the other's certificate
    EXPECT_EQ(mClient.session->getRemoteCertificateHash("sha-256"), mServer.certificate->getSha256FingerprintBin());
    EXPECT_EQ(mServer.session->getRemoteCertificateHash("sha-256"), mClient.certificate->getSha256FingerprintBin());
}

TEST_F(DtlsSessionTest, SrtpKeys)
{
    ASSERT_NO_FATAL_FAILURE(connect());

    const auto [clientKeys, clientError] = mClient.session->getSrtpKeys();
    const auto [serverKeys, serverError] = mServer.session->getSrtpKeys();

    ASSERT_TRUE(clientKeys.has_value()) << clientError.message;
    ASSERT_TRUE(serverKeys.has_value()) << serverError.message;

    EXPECT_EQ(clientKeys->profileId, serverKeys->profileId);
    EXPECT_FALSE(clientKeys->sendMasterKey.empty());
    EXPECT_FALSE(clientKeys->sendMasterSalt.empty());

    // What one side sends with, the other receives with
    EXPECT_TRUE(equals(clientKeys->sendMasterKey, serverKeys->receiveMasterKey));
    EXPECT_TRUE(equals(clientKeys->sendMasterSalt, serverKeys->receiveMasterSalt));
    EXPECT_TRUE(equals(clientKeys->receiveMasterKey, serverKeys->sendMasterKey));
    EXPECT_TRUE(equals(clientKeys->receiveMasterSalt, serverKeys->sendMasterSalt));

    // And the two directions use different keys
    EXPECT_FALSE(equals(clientKeys->sendMasterKey, clientKeys->receiveMasterKey));
}

TEST_F(DtlsSessionTest, ApplicationData)
{
    ASSERT_NO_FATAL_FAILURE(connect());

    ASSERT_TRUE(mClient.session->send(toBuffer("hello server")).isOk());
    ASSERT_TRUE(pump([this] { return !mServer.received.empty(); }));
    EXPECT_EQ(mServer.received.front(), "hello server");

    ASSERT_TRUE(mServer.session->send(toBuffer("hello client")).isOk());
    ASSERT_TRUE(pump([this] { return !mClient.received.empty(); }));
    EXPECT_EQ(mClient.received.front(), "hello client");

    EXPECT_TRUE(mClient.readError.empty()) << mClient.readError;
    EXPECT_TRUE(mServer.readError.empty()) << mServer.readError;
}

TEST_F(DtlsSessionTest, Close)
{
    ASSERT_NO_FATAL_FAILURE(connect());

    mClient.session->close();
    EXPECT_EQ(mClient.session->getState(), DtlsState::Closed);

    // The server gets close_notify
    ASSERT_TRUE(pump([this] { return mServer.eof; }));
    EXPECT_EQ(mServer.session->getState(), DtlsState::Closed);
}

TEST_F(DtlsSessionTest, WrongFingerprintOnServer)
{
    ASSERT_NO_FATAL_FAILURE(createSessions(make_hash(mServer.certificate), make_wrong_hash()));
    ASSERT_NO_FATAL_FAILURE(start());

    ASSERT_TRUE(pump([this] { return mServer.failed; }));
    EXPECT_FALSE(mServer.connected);
    EXPECT_EQ(mServer.session->getState(), DtlsState::Failed);
    EXPECT_NE(mServer.failureMessage.find("Certificate hash"), std::string::npos) << mServer.failureMessage;
}

TEST_F(DtlsSessionTest, WrongFingerprintOnClient)
{
    ASSERT_NO_FATAL_FAILURE(createSessions(make_wrong_hash(), make_hash(mClient.certificate)));
    ASSERT_NO_FATAL_FAILURE(start());

    ASSERT_TRUE(pump([this] { return mClient.failed; }));
    EXPECT_FALSE(mClient.connected);
    EXPECT_EQ(mClient.session->getState(), DtlsState::Failed);
    EXPECT_NE(mClient.failureMessage.find("Certificate hash"), std::string::npos) << mClient.failureMessage;
}

TEST_F(DtlsSessionTest, PacketLoss)
{
    // Lose the ClientHello, and the first packet of the server's first flight
    mClient.dropFirstCount = 1;
    mServer.dropFirstCount = 1;

    ASSERT_NO_FATAL_FAILURE(createSessions());
    ASSERT_NO_FATAL_FAILURE(start());

    // Only the retransmit timers can recover from this
    ASSERT_TRUE(pump([this] { return mClient.connected && mServer.connected; }));
    EXPECT_FALSE(mClient.failed) << mClient.failureMessage;
    EXPECT_FALSE(mServer.failed) << mServer.failureMessage;
}

TEST_F(DtlsSessionTest, NotConnected)
{
    ASSERT_NO_FATAL_FAILURE(createSessions());

    // Before the handshake
    EXPECT_TRUE(mClient.session->send(toBuffer("too early")).isError());
    EXPECT_FALSE(mClient.session->getSrtpKeys().first.has_value());

    ASSERT_NO_FATAL_FAILURE(start());

    // A session can only be started once
    EXPECT_TRUE(mClient.session->start().isError());
}
