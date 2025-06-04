#pragma once

namespace srtc
{

class PeerCandidate;
class Error;

class PeerCandidateListener
{
public:
	virtual ~PeerCandidateListener() = default;

    virtual void onCandidateHasDataToSend(PeerCandidate* candidate) = 0;

    virtual void onCandidateConnecting(PeerCandidate* candidate) = 0;
    virtual void onCandidateIceSelected(PeerCandidate* candidate) = 0;
    virtual void onCandidateConnected(PeerCandidate* candidate) = 0;
    virtual void onCandidateFailedToConnect(PeerCandidate* candidate, const Error& error) = 0;
};

} // namespace srtc
