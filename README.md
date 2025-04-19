### srtc - a simple WebRTC library

This is srtc, a simple WebRTC library (publish side only so far).

Features:

- Depends on OpenSSL (or BoringSSL) only, nothing else.
- Portable code in "conservative" C++
- Conservative means it uses C++ 17, can be made C++ 14 with little effort; does not use exceptions or RTTI.
- Only one worker thread per PeerConnection.
- Supports H264 (arbitrary profile ID) for video and Opus for audio. Would be easy to add H265 and other codecs.
- Video simulcast (sending multiple layers at different resolutions) including the Google VLA extension.
- SDP offer generation and SDP response parsing.
- Retransmits of packets reported lost by the receiver, which uses RTX if supported.
- Support for IPv4 and IPv6.
- Supports Linux including Android (MacOS needs an event loop based on kevent).
- Android demo has been tested with Pion and Amazon IVS (Interactive Video Service).
- ICE / STUN negotiation, DTLS negotiation, SRTP and SRTCP.

### Envisioned use case 

Can be used for publishing media from a server side system i.e. where network bandwidth is good and where you
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

- setVideoSingleCodecSpecificData
- publishSingleVideoFrame
- publishAudioFrame

The peer connection will maintain connectivity using STUN probe requests if no media is flowing and will attempt to
re-establish connectivity if there is no data received from the server. If the re-connection fails,
so will the connection's state.

For simulcast, the methods are similar but different:

- Configure your layers when generating the offer
- setVideoSimulcastCodecSpecificData
- publishVideoSimulcastFrame

### A command line demo / sample

Build the project using CMake.

Open a new terminal window, change directory to `pion-webrtc-examples-whip-whep` and execute `run.sh`. This will run the Pion
WebRTC server.

Open a new web browser window to `http://localhost:8080`, you will see a web page with controls for publishing and subscribing.
Click "Subscribe".

Now switch back to the terminal window where you built `srtc` and run `<your-cmake-dir>/cmdline_demo`, making sure the
current directory is the `srtc` directory. This will load a video file and send it to Pion using WHIP.

Switch back to the browser and click, after a second or two (keyframe delay) you should see the video being sent by `srtc`.

Note: my video sample is garbage, VLC shows it corrupted, and for some reason it's not playing well in Brave Browser. It does play just fine in Firefox. The Android demo plays fine in Brave.

### An Android demo / sample

There is an Android demo:

https://github.com/kmansoft/srtc-android-demo

Note that the code in this library is not Android specific, it cleanly builds  against Linux headers.

Android was chosen for the demo because it's an easy way to capture video and audio and encode them to
H264 and Opus. Sorry I'm not familiar with how to capture and encode media on desktop Linux.

For the interface between Android code and the srtc library, please see `jni_peer_connection.h / .cpp` in that project.

### Disclamier

I work for [Amazon IVS (Interactive Video Service)](https://ivs.rocks/).

This library is my side project.

### Future plans

First, I'd like to replace Cisco's SRTP library with my new code using OpenSSL / BoringSSL directly. This is done.

Second, I'd lke to implement support for Simulcast (multiple video layers on the same peer connection). This is done.

Third, Google's congestion control / bandwidth measurement extensions may be useful.

Fourth, support for more codecs can be added, but I currently only have access to systems which support H264. If you'd
like to see support for H265 / VP8 / VP8 / AV1 packetization, feel free to point me to a WHIP / WebRTC server which
supports those.  
