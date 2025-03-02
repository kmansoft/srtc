### srtc - a simple WebRTC library

This is srtc, a simple WebRTC library (publish side only so far).

Features:

- Depends on OpenSSL (or BoringSSL) and [Cisco's libSRTP](https://github.com/cisco/libsrtp).
- Portable code in "conservative" C++ (uses C++ 17, can be made C++ 14 with little effort; does not use exceptions or RTTI).
- Only one thread per PeerConnection.
- Supports H264 (arbitrary profile ID) for video and Opus for audio.
- SDP offer generation and SDP response parsing.
- Retransmits of packets reported lost by the received, which uses RTX if supported.
- Support for IPv4 and IPv6.
- Supports Linux including Android but might compile on MacOS too (not tested).
- Android demo has been tested with Pion and Amazon IVS (Interactive Video Service).
- ICE / STUN negotiation, DTLS negotiation, SRTP and SRTCP.

Envisioned use case - publishing media from a server side system i.e. where network bandwidth is good and where you
want to run multiple (hundreds or perhaps thousands) of simultaneous WebRTC sessions on a single computer so Google's
implementation is not a good choice due to its hunger for threads.

Media encoding is deliberately out of scope of this library. It expects the application to provide encoded media samples,
but does take care of packetization.

The API is deliberately not compatible with Google's, but close. The Google WebRTC library is inteded for browsers, and
therefore its API has to match that defined for JavaScript and cannot be changed. I decided that it's not necessary to
follow the JavaScript API.

### Basic use

Create a PeerConnection, ask it to create an SDP offer, send it to a WHIP server using your favorite HTTP library,
then set the SDP answer on the PeerConnection. This will initiate a connection and its state will be emitted
via the state callback.

Once connected, you can start sending audio and video samples using these methods:

- setVideoCodecSpecificData
- publishVideoFrame
- publishAudioFrame

The peer connection will maintain connectivity using STUN probe requests if no media is flowing and will attempt to
re-establish connectivity if there is no data received from the server. If the re-connection fails,
so will the connection's state.

### A detailed sample

There is an Android demo:

https://github.com/kmansoft/srtc-android-demo

Note that the code in this library is not Android specific, it cleanly builds  against Linux headers and I expect
it should compile and work on MacOS too but that's not been tested as I don't own a Mac.

Android was chosen for the demo because it's an easy way to capture video and audio and encode them to
H264 and Opus. Sorry I'm not familiar with how to capture and encode media on desktop Linux.

### Disclamier

I work for [Amazon IVS (Interactive Video Service)](https://ivs.rocks/) which is part of [Twitch](https://www.twitch.tv/).

This is my side project.

### Future plans

First, I'd like to get rid of Cisco's SRTP library and replace it with my own code using OpenSSL / BoringSSL directly.

Second, I'd lke to implement support for Simulcast (multiple video qualities on the same peer connection). There is a
problem with this though - since I work for IVS, I know how IVS handles Simulcast but this information is
not public, so I can't use this knowledge or publish code based on that. If you know of a WebRTC server which
I can use instead, please let me know.

Third, Google's congestion control / bandwidth measurement extensions may be useful. Extensions in general are
currently parsed from the SDP but are not implemented at the RTP level (not that it's difficult).
