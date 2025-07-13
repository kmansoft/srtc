#pragma once

#include <memory>

namespace srtc
{

class PeerCandidate;
class Error;
class RtpPacket;
class Track;

struct SenderReport;

class PeerCandidateListener
{
public:
    virtual ~PeerCandidateListener() = default;

    virtual void onCandidateHasDataToSend(PeerCandidate* candidate) = 0;

    virtual void onCandidateConnecting(PeerCandidate* candidate) = 0;
    virtual void onCandidateIceSelected(PeerCandidate* candidate) = 0;
    virtual void onCandidateConnected(PeerCandidate* candidate) = 0;
    virtual void onCandidateFailedToConnect(PeerCandidate* candidate, const Error& error) = 0;

    virtual void onCandidateReceivedMediaPacket(PeerCandidate* candiate, const std::shared_ptr<RtpPacket>& packet) = 0;
    virtual void onCandidateReceivedSenderReport(PeerCandidate* candidate,
                                                 const std::shared_ptr<Track>& track,
                                                 const SenderReport& sr) = 0;
};

} // namespace srtc
