### srtc - a simple WebRTC library

This is srtc, a simple WebRTC library (publish side only so far).

Features:

- Depends on OpenSSL (or BoringSSL) only, nothing else.
- Portable code in "conservative" C++
- Conservative means it uses C++ 17, can be made C++ 14 with little effort; does not use exceptions or RTTI.
- Only one worker thread per PeerConnection.
- Supports H264 (any profile ID) for video and Opus for audio. Would be easy to add H265 and other codecs.
- Video simulcast (sending multiple layers at different resolutions) including the Google VLA extension.
- SDP offer generation and SDP response parsing.
- Retransmits of packets reported lost by the receiver, which uses RTX if supported.
- Support for IPv4 and IPv6.
- Android demo and Mac demo have been tested with Pion and Amazon IVS (Interactive Video Service).
- Command line demo has been tested with Pion.
- ICE / STUN negotiation, DTLS negotiation, SRTP and SRTCP.
- Works on Linux, Android, MacOS, and should work on iOS too.

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
then set the SDP answer on the PeerConnection. This will initiate a network connection and its state will be emitted
via the state callback.

Once connected, you can start sending audio and video samples using these methods:

- setVideoSingleCodecSpecificData
- publishSingleVideoFrame
- publishAudioFrame

For simulcast, the methods are similar but different:

- Configure your layers when generating the offer
- setVideoSimulcastCodecSpecificData
- publishVideoSimulcastFrame

The peer connection will maintain connectivity using STUN probe requests if no media is flowing and will attempt to
re-establish connectivity if there is no data received from the server. If the re-connection fails, so will the
overall connection state.

### A command line demo / sample

Tested on Linux and MacOS with Pion.

If not using the default `.h264` file, you can convert an `.mp4` to raw H.264 with FFMPEG. Make sure to use the `baseline` profile.

```bash
ffmpeg -i /path/to/a.mp4 -c:v libx264 -profile:v baseline -level 3.0 -preset medium -an -f h264 out.h264
```

Build the project using CMake.

```bash
cmake . -B build
```

Change into the build directory and run:

```bash
make
```

Change back to the root (`cd ../`) and run the command line demo. Use `--help` to see arguments.

```bash
./build/cmdline_demo --help
```
Should output:

```bash
Usage: ./build/cmdline_demo [options]
Options:
  -f, --file <path>    Path to H.264 file (default: sintel.h264)
  -u, --url <url>      WHIP server URL (default: https://localhost:8080)
  -t, --token <token>  WHIP authorization token
  -h, --help           Show this help message
```

To broadcast to Amazon IVS:

```bash
./build/cmdline_demo -f /path/to/out.h264 -u https://global.whip.live-video.net -t [YOUR STAGE TOKEN]
```

### Running with Pion

Open a new terminal window, change the directory to `pion-webrtc-examples-whip-whep` and execute `run.sh`. This will run the Pion
WebRTC server.

Open a new web browser window to `http://localhost:8080`, you will see a web page with controls for publishing and subscribing.
Click "Subscribe", you should see "Checking" / "Connected" in the status area below and there should be a progress wheel
over the video area.

Now switch back to the terminal window where you built `srtc` and run `<your-cmake-dir>/cmdline_demo`, making sure the
current directory is the `srtc` directory. This will load a video file and send it to Pion using WHIP.

Switch back to the browser and click, after a second or two (keyframe delay) you should see the video being sent by `srtc`.

### An Android demo / sample

There is an Android demo:

https://github.com/kmansoft/srtc-android-demo

Note that the code in this library is not Android specific, only the demo app is.

Android was chosen for the demo because it's an easy way to capture video and audio and encode them to
H264 and Opus. Sorry I'm not familiar with how to capture and encode media on desktop Linux.

For the interface between Android code and the srtc library, please see `jni_peer_connection.h / .cpp` in that project.

### A MacOS demo / sample

There is a MacOS demo:

https://github.com/kmansoft/srtc-macos-demo

Note that the code in this library is not MacOS specific, only the demo app is.

This demo also captures the camera and microphone and publishes them as H264 and Opus to Pion or Amazon IVS.

### Disclamier

I work for [Amazon IVS (Interactive Video Service)](https://ivs.rocks/).

This library is my side project.

### Future plans

- I'd like to replace Cisco's SRTP library with my new code using OpenSSL / BoringSSL directly. This is done.

- I'd lke to implement support for Simulcast (multiple video layers on the same peer connection). This is done.

- Google's Transport Wide Congestion Control.

- Support for more codecs can be added, but I currently only have access to systems which support H264. If you'd
like to see support for H265 / VP8 / VP8 / AV1 packetization, feel free to point me to a WHIP / WebRTC server which
supports those.

