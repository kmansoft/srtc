#pragma once

namespace srtc
{

class PeerCandidate;
class Error;

class PeerCandidateListener
{
public:
    virtual void onCandidateHasDataToSend(PeerCandidate* candidate) = 0;

    virtual void onCandidateConnecting(PeerCandidate* candidate) = 0;
    virtual void onCandidateIceConnected(PeerCandidate* candidate) = 0;
    virtual void onCandidateDtlsConnected(PeerCandidate* candidate) = 0;
    virtual void onCandidateFailedToConnect(PeerCandidate* candidate, const Error& error) = 0;
};

} // namespace srtc
