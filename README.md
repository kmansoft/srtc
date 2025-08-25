### srtc - a "simple" WebRTC library

This is srtc, a "simple" WebRTC library (publish side is done and working quite well, subscribe is in progress, works but needs more time to mature).

#### Features:

- Depends on OpenSSL (or BoringSSL) only, nothing else.
- Portable code in "conservative" C++: language level is C++ 17, and no exceptions or RTTI.
- Only one worker thread per PeerConnection.
- Video codecs: VP8, H264 (any profile ID), more are coming.
- Audo codec: Opus.
- SDP offer generation and SDP response parsing.
- ICE / STUN negotiation, DTLS negotiation, SRTP and SRTCP.
- Support for IPv4 and IPv6.
- Tested with Pion and Amazon IVS (Interactive Video Service).
- Works on Linux, Android, MacOS, Windows, and should work on iOS too.

#### State of publish

- Retransmits of packets reported lost by the receiver, uses RTX if supported.
- Video simulcast (sending multiple layers at different resolutions) including the Google VLA extension.
- Basic bandwidth estimation using the TWCC extension and probing.
- Pacing.

#### State of subscribe

- Sends nacks, understands RTX.
- Sends PLI (key frame requests).
- Sends receiver reports.
- Sends TWCC reports if negotiated in the SDP.
- The jitter buffer is fixed size (for now), based on RTT estimates from the ICE exchange while connecting.
- No explicit media synchronization based on NTP timestamps in sender reports (yet), so your audio and video may be a millisecond or a few out of sync. Both are reported to the application though, so it could be handled there.
- No speed up / slow down for audio samples (yet).

### API design 

Efficient. Can be used for publishing media from a server side system i.e. where you want to run multiple (hundreds or
perhaps thousands) of simultaneous WebRTC sessions on a single computer so Google's implementation is not a good choice
due to its hunger for threads.

Has a command line tool to publish video, [which has already seen some use](https://www.linkedin.com/posts/toddrsharp_releases-kmansoftsrtc-activity-7342987919445385216-N74_?utm_source=share&utm_medium=member_desktop&rcm=ACoAADsOqaEBZ5sFObLsqWe6Ii4d-zOg-Q6-iVM).

Has a command line tool to subscribe to audio and/or video, which can save media to .ogg and .h264 files.

Media encoding / decoding and presentation are deliberately out of scope of this library. For publishing, the application needs to
provide encoded media samples. For subscribing, the application receives encoded media samples which it needs to decode and present.

The API is deliberately not compatible with Google's, but the concepts are similar. The Google WebRTC library is inteded
for browsers, and therefore its API has to match the API defined for JavaScript and cannot be changed. I decided that it's
not necessary to follow the JavaScript API.

### Basic use from C++

Create a PeerConnection, ask it to create an SDP offer, send it to a WHIP / WHEP server using your favorite HTTP library,
then set the SDP answer on the PeerConnection. This will initiate a network connection, whose state will be emitted via the connection
state callback.

Once the peer is connected, you can start publishing audio and video samples using these methods:

- setVideoSingleCodecSpecificData
- publishVideoSingleFrame
- publishAudioFrame

For simulcast, the methods are similar but different:

- Configure your layers when generating the offer
- setVideoSimulcastCodecSpecificData
- publishVideoSimulcastFrame

For subscribing, use the `setSubscribeEncodedFrameListener` method to receive encoded frames as they come out of the jitter buffer.

The peer connection will maintain connectivity using STUN probe requests if no media is flowing and will attempt to
re-establish connectivity as needed. If the re-connection fails, so will the overall connection state.

### A command line tool for publishing

Tested on Linux, MacOS, Windows with Pion and Amazon IVS.

If not using the provided `.h264` files, you can convert an `.mp4` to raw H.264 with FFMPEG. Make sure to use the `baseline` profile.

```bash
ffmpeg -i /path/to/a.mp4 -c:v libx264 -profile:v baseline -level 3.0 \
    -preset medium -an -f h264 out.h264
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
  -f, --file <path>    Path to a H.264 or VP8 (webm) file (default: sintel.h264)
  -u, --url <url>      WHIP server URL (default: http://localhost:8080/whip)
  -t, --token <token>  WHIP authorization token
  -l, --loop           Loop the file
  -v, --verbose        Verbose logging from the srtc library
  -q, --quiet          Suppress progress reporting
  -s, --sdp            Print SDP offer and answer
  -i, --info           Print input file info
  -d, --drop           Drop some packets at random (test NACK and RTX handling)
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

#### Using a VP8 input file

First please run the Pion WebRTC server like this to use VP8 (by default it uses H264):

```bash
./run.sh -vp8
```

To send video to Pion, run the publish sample like this:

```bash
./build/srtc_publish[.exe] -f sintel-vp8.webm
```

### A command line tool for subscribing

```bash
./build/srtc_subscribe[.exe] --help
```

Should output:

```bash
Usage: srtc_subscribe [options]
Options:
  -u, --url <url>      WHEP server URL (default: http://localhost:8080/whep)
  -t, --token <token>  WHEP authorization token
  -v, --verbose        Verbose logging from the srtc library
  -q, --quiet          Suppress progress reporting
  -s, --sdp            Print SDP offer and answer
  --oa <filename>      Save audio to a file (ogg format for opus)
  --ov <filename>      Save video to a file (h264 or webm format)
  -d, --drop           Drop some packets at random (test NACK and RTX handling)
  -h, --help           Show this help message
```

The subscribe tool handles Ctrl+C and SIGTERM and terminates gracefully, flushing and closing the output files.

Note that *.h264 files have no frame rate information, and so may play "very fast" depending on your video player. If using
VLC, you can use the below option to adjust playback speed:

```bash
      --rate <float>             Playback speed
```

#### Running in VP8 mode

Run the Pion server like this, just like for publishing:

```bash
./run.sh -vp8
```

And then subscribe like this:

```bash
./build/srtc_subscribe[.exe] --ov output.webm
```

The resulting webm file will not contain any audio, just video - if you'd like to capture audio as well,
please add `--oa output.ogg`.

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

- Replace Cisco's SRTP library with my new code using OpenSSL / BoringSSL directly. This is done.

- Implement support for Simulcast (multiple video layers on the same peer connection). This is done.

- Support Google's Transport Wide Congestion Control. This is mostly done and will continue to improve.

- Windows port. This is done.

- Releases. This is done.

- Start implementing subscribing. In progress.
  
- Support for more codecs. If you'd  like to see support for H265 and/or AV1, feel free to point me to a WebRTC server which supports those.
