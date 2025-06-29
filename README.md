### srtc - a "simple" WebRTC library

This is srtc, a "simple" WebRTC library (publish side only so far, the work to implement subscribe has started).

Features:

- Depends on OpenSSL (or BoringSSL) only, nothing else.
- Portable code in "conservative" C++: language level is C++ 17, and no exceptions or RTTI.
- Only one worker thread per PeerConnection.
- Supports H264 (any profile ID) for video and Opus for audio. Would be easy to add H265 and other codecs.
- Video simulcast (sending multiple layers at different resolutions) including the Google VLA extension.
- SDP offer generation and SDP response parsing.
- ICE / STUN negotiation, DTLS negotiation, SRTP and SRTCP.
- Retransmits of packets reported lost by the receiver, which uses RTX if supported.
- Support for IPv4 and IPv6.
- Pacing.
- Basic bandwidth estimation using the TWCC extension and probing.
- Tested with Pion and Amazon IVS (Interactive Video Service).
- Works on Linux, Android, MacOS, Windows, and should work on iOS too.

### API design 

Efficient. Can be used for publishing media from a server side system i.e. where you want to run multiple (hundreds or
perhaps thousands) of simultaneous WebRTC sessions on a single computer so Google's implementation is not a good choice
due to its hunger for threads.

Has a simple command line tool to publish video, [which has already seen some use](https://www.linkedin.com/posts/toddrsharp_releases-kmansoftsrtc-activity-7342987919445385216-N74_?utm_source=share&utm_medium=member_desktop&rcm=ACoAADsOqaEBZ5sFObLsqWe6Ii4d-zOg-Q6-iVM).

Media encoding is deliberately out of scope of this library. It expects the application to provide encoded media samples,
but does take care of packetization.

The API is deliberately not compatible with Google's, but the concepts are similar. The Google WebRTC library is inteded
for browsers, and therefore its API has to match the API defined for JavaScript and cannot be changed. I decided that it's
not necessary to follow the JavaScript API.

### Basic use

Create a PeerConnection, ask it to create an SDP offer, send it to a WHIP server using your favorite HTTP library,
then set the SDP answer on the PeerConnection. This will initiate a network connection and its state will be emitted
via the state callback.

Once connected, you can start sending audio and video samples using these methods:

- setVideoSingleCodecSpecificData
- publishVideoSingleFrame
- publishAudioFrame

For simulcast, the methods are similar but different:

- Configure your layers when generating the offer
- setVideoSimulcastCodecSpecificData
- publishVideoSimulcastFrame

The peer connection will maintain connectivity using STUN probe requests if no media is flowing and will attempt to
re-establish connectivity if there is no data received from the server. If the re-connection fails, so will the
overall connection state.

### A command line tool for publishing

Tested on Linux, MacOS, Windows with Pion and Amazon IVS.

If not using the provided `.h264` files, you can convert an `.mp4` to raw H.264 with FFMPEG. Make sure to use the `baseline` profile.

```bash
ffmpeg -i /path/to/a.mp4 -c:v libx264 -profile:v baseline -level 3.0 -preset medium -an -f h264 out.h264
```

Build the project using CMake.

```bash
cmake . -B build
```

Change into the build directory and run:

```bash
cmake --build .
```

Change back to the root (`cd ..`) and run the command line demo. Use `--help` to see arguments.

```bash
./build/srtc_publish[.exe] --help
```

Should output:

```bash
Usage: ./build/srtc_publish [options]
Options:
  -f, --file <path>    Path to H.264 file (default: sintel.h264)
  -u, --url <url>      WHIP server URL (default: http://localhost:8080/whip)
  -t, --token <token>  WHIP authorization token
  -l, --loop           Loop the file
  -v, --verbose        Verbose logging from the srtc library
  -q, --quiet          Suppress progress reporting
  -s, --sdp            Print SDP offer and answer
  -i, --info           Print input file info
  -d, --drop           Drop some packets at random (test NCK and RTX handling)
  -b, --bwe            Enable TWCC congestion control for bandwidth estimation
  -h, --help           Show this help message
```

#### To broadcast to Amazon IVS:

```bash
./build/srtc_publish -f /path/to/out.h264 -u https://global.whip.live-video.net -t [YOUR STAGE TOKEN]
```

#### Testing with Pion

Open a new terminal window, change the directory to `pion-webrtc-examples-whip-whep` and execute `run.sh` or `go run .` to
start the Pion  WebRTC server.

Open a new web browser window to `http://localhost:8080`, you will see a web page with controls for publishing and subscribing.
Click "Subscribe", you should see "Checking" / "Connected" in the status area below and there should be a progress wheel
over the video area.

Now switch back to the terminal window where you built `srtc` and run `<your-cmake-dir>/srtc_publish[.exe]`, making sure the
current directory is the `srtc` directory. This will load a video file and send it to Pion using WHIP.

Switch back to the browser, after a second or two (keyframe delay) you should see the video being sent by `srtc`.

### An Android demo / sample

There is an Android demo:

https://github.com/kmansoft/srtc-android-demo

Note that the code in this library is not Android specific, only the demo app is.

This demo also captures the camera and microphone and publishes them as H264 and Opus to Pion or Amazon IVS.

For the interface between Android code and the srtc library, please see `jni_peer_connection.h / .cpp` in that project.

### A MacOS demo / sample

There is a MacOS demo:

https://github.com/kmansoft/srtc-macos-demo

Note that the code in this library is not MacOS specific, only the demo app is.

This demo also captures the camera and microphone and publishes them as H264 and Opus to Pion or Amazon IVS.

The interface between Swift code of the app and C++ code in srtc is in the `srtc-mac` subdirectory.

### Disclamier

I work for [Amazon IVS (Interactive Video Service)](https://ivs.rocks/).

This library is my side project.

### Future plans

- I'd like to replace Cisco's SRTP library with my new code using OpenSSL / BoringSSL directly. This is done.

- I'd lke to implement support for Simulcast (multiple video layers on the same peer connection). This is done.

- Google's Transport Wide Congestion Control. This is mostly done and will continue to improve.

- Windows port. This is done.

- Releases. This is done.

- Start implementing subscribing. A very large piece of work. Started.

- Support for more codecs can be added, but I currently only have access to systems which support H264. If you'd
like to see support for H265 / VP8 / VP8 / AV1 packetization, feel free to point me to a WHIP / WebRTC server which
supports those.
