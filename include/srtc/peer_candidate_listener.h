#pragma once

#include <memory>
#include <vector>

namespace srtc
{

class ByteBuffer;
class PeerCandidate;
class Error;
class RtpPacket;
class Track;

struct SenderReport;
struct SimulcastLayer;

class PeerCandidateListener
{
public:
    virtual ~PeerCandidateListener();

    virtual void onCandidateHasDataToSend(PeerCandidate* candidate) = 0;

    virtual void onCandidateConnecting(PeerCandidate* candidate) = 0;
    virtual void onCandidateIceConnected(PeerCandidate* candidate) = 0;
    virtual void onCandidateDtlsConnected(PeerCandidate* candidate) = 0;
    virtual void onCandidateDtlsDisconnected(PeerCandidate* candidate, const Error& error) = 0;
    virtual void onCandidateFailedToConnect(PeerCandidate* candidate, const Error& error) = 0;

    virtual void onCandidateReceivedMediaPacket(PeerCandidate* candiate, const std::shared_ptr<RtpPacket>& packet) = 0;
    virtual void onCandidateReceivedSenderReport(PeerCandidate* candidate,
                                                 const std::shared_ptr<Track>& track,
                                                 const SenderReport& sr) = 0;

    virtual void onCandidateReceivedKeyFrameRequest(PeerCandidate* candiate) = 0;

    [[nodiscard]] virtual const std::vector<SimulcastLayer>& getSimulcastLayerList() const = 0;

    virtual void onSctpDataChannelOpen(const std::string& label) = 0;
    virtual void onSctpDataChannelText(const std::string& label, const std::string& data) = 0;
    virtual void onSctpDataChannelBinary(const std::string& label, const ByteBuffer& data) = 0;
    virtual void onSctpDataChannelClosed(const std::string& label) = 0;
};

} // namespace srtc
