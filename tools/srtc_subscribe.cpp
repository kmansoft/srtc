#include "srtc/encoded_frame.h"
#include "srtc/h264.h"
#include "srtc/logging.h"
#include "srtc/peer_connection.h"
#include "srtc/sdp_answer.h"
#include "srtc/sdp_offer.h"
#include "srtc/track.h"

#include "media_writer_h26x.h"
#include "media_writer_ogg.h"

#include "http_whip_whep.h"

#include <csignal>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// Program options

static std::string gWhepUrl = "http://localhost:8080/whep";
static std::string gAuthToken = "none";
static bool gQuiet = false;
static bool gPrintSDP = false;
static std::string gOutputAudioFilename;
static std::string gOutputVideoFilename;
static bool gDropPackets = false;

// Signals

static bool gSigInterrupt = false;
static bool gSigTerminate = false;

void signalHandler(int signal)
{
    if (signal == SIGINT) {
        gSigInterrupt = true;
    } else if (signal == SIGTERM) {
        gSigTerminate = true;
    }
}

// State

static std::atomic_bool gIsConnectionFailed = false;

void printUsage(const char* programName)
{
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -u, --url <url>      WHEP server URL (default: " << gWhepUrl << ")" << std::endl;
    std::cout << "  -t, --token <token>  WHEP authorization token" << std::endl;
    std::cout << "  -v, --verbose        Verbose logging from the srtc library" << std::endl;
    std::cout << "  -q, --quiet          Suppress progress reporting" << std::endl;
    std::cout << "  -s, --sdp            Print SDP offer and answer" << std::endl;
    std::cout << "  --oa <filename>      Save audio to a file (ogg format for opus)" << std::endl;
    std::cout << "  --oa <filename>      Save video to a file (h264 format)" << std::endl;
    std::cout << "  -d, --drop           Drop some packets at random (test NACK and RTX handling)" << std::endl;
    std::cout << "  -h, --help           Show this help message" << std::endl;
}

const char* connectionStateToString(const srtc::PeerConnection::ConnectionState& state)
{
    switch (state) {
    case srtc::PeerConnection::ConnectionState::Inactive:
        return "inactive";
    case srtc::PeerConnection::ConnectionState::Connecting:
        return "connecting";
    case srtc::PeerConnection::ConnectionState::Connected:
        return "connected";
    case srtc::PeerConnection::ConnectionState::Failed:
        return "failed";
    case srtc::PeerConnection::ConnectionState::Closed:
        return "closed";
    default:
        return "?";
    }
}

int main(int argc, char* argv[])
{
    // Set logging to warnings by default
    srtc::setLogLevel(SRTC_LOG_W);

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-u" || arg == "--url") {
            if (i + 1 < argc) {
                gWhepUrl = argv[++i];
            } else {
                std::cerr << "Error: -u/--url requires a URL" << std::endl;
                return 1;
            }
        } else if (arg == "-t" || arg == "--token") {
            if (i + 1 < argc) {
                gAuthToken = argv[++i];
            } else {
                std::cerr << "Error: -t/--token requires a token value" << std::endl;
                return 1;
            }
        } else if (arg == "-v" || arg == "--verbose") {
            srtc::setLogLevel(SRTC_LOG_V);
        } else if (arg == "-q" || arg == "--quiet") {
            gQuiet = true;
        } else if (arg == "-s" || arg == "--sdp") {
            gPrintSDP = true;
        } else if (arg == "--oa") {
            if (i + 1 < argc) {
                gOutputAudioFilename = argv[++i];
            } else {
                std::cerr << "Error: --oa requires a filename" << std::endl;
                return 1;
            }
        } else if (arg == "--ov") {
            if (i + 1 < argc) {
                gOutputVideoFilename = argv[++i];
            } else {
                std::cerr << "Error: --ov requires a filename" << std::endl;
                return 1;
            }
        } else if (arg == "-d" || arg == "--drop") {
            gDropPackets = true;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    std::cout << "*** Using WHEP URL: " << gWhepUrl << std::endl;

    using namespace srtc;

    char cwd[1024];
#ifdef _WIN32
    if (!GetCurrentDirectoryA(sizeof(cwd), cwd)) {
        std::cout << "*** Cannot get current working directory" << std::endl;
        exit(1);
    }
#else
    if (!getcwd(cwd, sizeof(cwd))) {
        std::cout << "*** Cannot get current working directory" << std::endl;
        exit(1);
    }
#endif

    std::cout << "*** Current working directory: " << cwd << std::endl;

    // Peer connection state
    std::mutex connectionStateMutex;
    PeerConnection::ConnectionState connectionState = PeerConnection::ConnectionState::Inactive;
    std::condition_variable connectionStateCond;

    // Peer connection
    auto connectedReported = false;
    const auto ms0 = std::chrono::steady_clock::now();
    const auto peerConnection = std::make_shared<PeerConnection>(Direction::Subscribe);

    peerConnection->setConnectionStateListener(
        [ms0, &connectedReported, &connectionStateMutex, &connectionState, &connectionStateCond](
            const PeerConnection::ConnectionState& state) {
            if (state == PeerConnection::ConnectionState::Connected && !connectedReported) {
                const auto ms1 = std::chrono::steady_clock::now();
                const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(ms1 - ms0).count();
                std::cout << "*** PeerConnection state: " << connectionStateToString(state) << " in " << millis
                          << " millis" << std::endl;
                connectedReported = true;
            } else {
                std::cout << "*** PeerConnection state: " << connectionStateToString(state) << std::endl;
            }

            if (state == PeerConnection::ConnectionState::Failed) {
                gIsConnectionFailed = true;
            }

            {
                std::lock_guard lock(connectionStateMutex);
                connectionState = state;
            }
            connectionStateCond.notify_one();
        });

    // Offer
    SubOfferConfig offerConfig = {};
    offerConfig.cname = "foo";
    offerConfig.debug_drop_packets = gDropPackets;

    SubVideoCodec videoCodec = {};
    videoCodec.codec = Codec::H264;
    videoCodec.profile_level_id = 0x42e01f;

    SubVideoConfig videoConfig = {};
    videoConfig.codec_list.push_back(videoCodec);

    SubAudioCodec audioCodec = {};
    audioCodec.codec = Codec::Opus;
    audioCodec.minptime = 20;
    audioCodec.stereo = true;

    SubAudioConfig audioConfig = {};
    audioConfig.codec_list.push_back(audioCodec);

    const auto [offer, offerCreateError] = peerConnection->createSubscribeOffer(offerConfig, videoConfig, audioConfig);
    if (offerCreateError.isError()) {
        std::cout << "Error: cannot create offer: " << offerCreateError.message << std::endl;
        exit(1);
    }

    const auto [offerString, offerStringError] = offer->generate();
    if (offerStringError.isError()) {
        std::cout << "Error: cannot generate offer: " << offerStringError.message << std::endl;
        exit(1);
    }
    if (gPrintSDP) {
        std::cout << "----- SDP offer -----\n" << offerString << std::endl;
    }

    // WHEP
    const auto answerString = perform_whip_whep(offerString, gWhepUrl, gAuthToken);
    if (gPrintSDP) {
        std::cout << "----- SDP answer -----\n" << answerString << std::endl;
    }

    // Answer
    const auto [answer, answerError] = peerConnection->parseSubscribeAnswer(offer, answerString, nullptr);
    if (answerError.isError()) {
        std::cout << "Error: cannot parse answer: " << answerError.message << std::endl;
        exit(1);
    }

    // Media writers
    std::shared_ptr<MediaWriter> mediaWriterAudio;
    std::shared_ptr<MediaWriter> mediaWriterVideo;

    if (!gOutputAudioFilename.empty()) {
        const auto track = answer->getAudioTrack();
        if (!track) {
            std::cout << "Saving audio output is requested, but there is no audio track" << std::endl;
            exit(1);
        } else if (track->getCodec() == srtc::Codec::Opus) {
            mediaWriterAudio = std::make_shared<MediaWriterOgg>(gOutputAudioFilename, track);
            mediaWriterAudio->start();
        } else {
            std::cout << "Saving audio output is requested, but the audio codec is not one we support" << std::endl;
            exit(1);
        }
    }

    if (!gOutputVideoFilename.empty()) {
        const auto track = answer->getVideoSingleTrack();
        if (!track) {
            std::cout << "Saving audio output is requested, but there is no video track" << std::endl;
            exit(1);
        } else if (track->getCodec() == srtc::Codec::H264) {
            mediaWriterVideo = std::make_shared<MediaWriterH26x>(gOutputVideoFilename, track);
            mediaWriterVideo->start();
        } else {
            std::cout << "Saving video output is requested, but the video codec is not one we support" << std::endl;
            exit(1);
        }
    }

    uint32_t frameCount = 0;
    std::chrono::steady_clock::time_point frameReportTime;

    peerConnection->setSubscribeEncodedFrameListener(
        [&frameCount, &frameReportTime, mediaWriterAudio, mediaWriterVideo](
            const std::shared_ptr<EncodedFrame>& frame) {

            const auto now = std::chrono::steady_clock::now();
            if (frameCount++ == 0) {
                frameReportTime = now;
            } else if (now - frameReportTime >= std::chrono::seconds(5)) {
                frameReportTime = now;
                std::cout << "*** Received " << frameCount << " frames of audio / video media" << std::endl;
            }

            const auto mediaType = frame->track->getMediaType();
            if (mediaType == srtc::MediaType::Audio) {
                if (mediaWriterAudio) {
                    mediaWriterAudio->send(frame);
                }
            } else if (mediaType == srtc::MediaType::Video) {
                if (mediaWriterVideo) {
                    mediaWriterVideo->send(frame);
                }
            }
        });

    // Connect the peer connection
    peerConnection->setOffer(offer);
    peerConnection->setAnswer(answer);

    // Wait for connection to either be connected or fail
    {
        std::unique_lock lock(connectionStateMutex);
        connectionStateCond.wait_for(lock, std::chrono::seconds(15), [&connectionState]() {
            return connectionState == PeerConnection::ConnectionState::Connected ||
                   connectionState == PeerConnection::ConnectionState::Failed;
        });

        if (connectionState != PeerConnection::ConnectionState::Connected) {
            std::cout << "*** Failed to connect" << std::endl;
            exit(1);
        }
    }

    // Set handlers for ctrl+c and term
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Run loop
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (gSigInterrupt) {
            std::cout << "Ctrl+C pressed, exiting..." << std::endl;
            break;
        }
        if (gSigTerminate) {
            std::cout << "Termination requested, exiting..." << std::endl;
            break;
        }
        if (gIsConnectionFailed) {
            std::cout << "The connection has failed, exiting..." << std::endl;
            break;
        }
    }

    // Wait a little and exit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    peerConnection->close();

    return 0;
}
