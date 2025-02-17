#pragma once

namespace srtc {

class PeerCandidate;
class Error;

class PeerCandidateListener {
public:
    virtual void onCandidateHasDataToSend(PeerCandidate* candidate) = 0;

    virtual void onCandidateConnecting(PeerCandidate* candidate) = 0;
    virtual void onCandidateConnected(PeerCandidate* candidate) = 0;
    virtual void onCandidateFailed(PeerCandidate* candidate, const Error& error) = 0;
};


}