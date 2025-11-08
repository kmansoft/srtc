#include "srtc/codec_h264.h"
#include "srtc/encoded_frame.h"
#include "srtc/logging.h"
#include "srtc/peer_connection.h"
#include "srtc/sdp_answer.h"
#include "srtc/sdp_offer.h"
#include "srtc/track.h"

#include "media_writer_av1.h"
#include "media_writer_h26x.h"
#include "media_writer_ogg.h"
#include "media_writer_vp8.h"

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
static bool gPrintSenderReports = false;
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
    std::cout << "  -r, --sr             Print sender report information" << std::endl;
    std::cout << "  -s, --sdp            Print SDP offer and answer" << std::endl;
    std::cout << "  --oa <filename>      Save audio to a file (ogg format for opus)" << std::endl;
    std::cout << "  --ov <filename>      Save video to a file (h264 or webm format)" << std::endl;
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

struct SenderReportState {
    srtc::ExtendedValue<uint32_t> rtp_ext;
    std::optional<uint64_t> last_rtp_ext;
    std::optional<srtc::SenderReport> last_report = {};
    std::optional<std::chrono::steady_clock::time_point> last_received = {};
};

void printSenderReport(const std::shared_ptr<srtc::Track>& track, const srtc::SenderReport& sr)
{
    static SenderReportState stateAudio;
    static SenderReportState stateVideo;

    const auto type = track->getMediaType();

    const char* label;
    SenderReportState* state;
    if (type == srtc::MediaType::Audio) {
        label = "AUDIO";
        state = &stateAudio;
    } else if (type == srtc::MediaType::Video) {
        label = "VIDEO";
        state = &stateVideo;
    } else {
        return;
    }

    const auto clock_rate = track->getClockRate();

    const auto rtp_ext = state->rtp_ext.extend(sr.rtp);

    const auto now = std::chrono::steady_clock::now();
    const auto sr_ntp_unix_micros = srtc::getNtpUnixMicroseconds(sr.ntp);

    std::string elapsed_wall_clock_s = "N/A";
    if (state->last_received.has_value()) {
        const auto elapsed_mills =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - state->last_received.value()).count();
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%6ld ms", static_cast<long>(elapsed_mills));
        elapsed_wall_clock_s = buf;
    }

    std::string ntp_diff_s = "N/A";
    if (state->last_report.has_value()) {
        const auto elapsed_micros = sr_ntp_unix_micros - srtc::getNtpUnixMicroseconds(state->last_report.value().ntp);
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%6ld ms", static_cast<long>(elapsed_micros / 1000));
        ntp_diff_s = buf;
    }

    std::string rtp_diff_s = "N/A";
    if (state->last_rtp_ext.has_value()) {
        const auto elapsed_millis = (1000l * (rtp_ext - state->last_rtp_ext.value())) / track->getClockRate();
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%6ld ms", static_cast<long>(elapsed_millis));
        rtp_diff_s = buf;
    }

    state->last_rtp_ext = rtp_ext;
    state->last_report = sr;
    state->last_received = now;

    std::printf(">>> SR for %s: ntp = [%12" PRIu32 ". %12" PRIu32 "], ntp unix = %14" PRId64
                " ms, ntp diff = %10s, rtp = %12" PRId64 ", rtp diff = %10s, elapsed time = %s\n",
                label,
                sr.ntp.seconds,
                sr.ntp.fraction,
                sr_ntp_unix_micros / 1000,
                ntp_diff_s.c_str(),
                rtp_ext,
                rtp_diff_s.c_str(),
                elapsed_wall_clock_s.c_str());
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
        } else if (arg == "-r" || arg == "--sr") {
            gPrintSenderReports = true;
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

    if (gPrintSenderReports) {
        peerConnection->setSubscribeSenderReportsListener(
            [](const std::shared_ptr<Track>& track, const SenderReport& sr) { printSenderReport(track, sr); });
    }

    // Offer
    SubOfferConfig offerConfig = {};
    offerConfig.cname = "foo";
    offerConfig.debug_drop_packets = gDropPackets;

    SubVideoCodec videoCodecVP8 = {};
    videoCodecVP8.codec = Codec::VP8;

    SubVideoCodec videoCodecH264 = {};
    videoCodecH264.codec = Codec::H264;
    videoCodecH264.profile_level_id = 0x42e01f;

    SubVideoCodec videoCodecH265 = {};
    videoCodecH265.codec = Codec::H265;

    SubVideoCodec videoCodecAV1 = {};
    videoCodecAV1.codec = Codec::AV1;

    SubVideoConfig videoConfig = {};
    videoConfig.codec_list.push_back(videoCodecVP8);
    videoConfig.codec_list.push_back(videoCodecH264);
    videoConfig.codec_list.push_back(videoCodecH265);
    videoConfig.codec_list.push_back(videoCodecAV1);

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
        }

        const auto codec = track->getCodec();
        if (codec == srtc::Codec::Opus) {
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
        }

        const auto codec = track->getCodec();
        if (codec == srtc::Codec::VP8) {
            mediaWriterVideo = std::make_shared<MediaWriterVP8>(gOutputVideoFilename, track);
            mediaWriterVideo->start();
        } else if (codec == srtc::Codec::H264 || codec == srtc::Codec::H265) {
            mediaWriterVideo = std::make_shared<MediaWriterH26x>(gOutputVideoFilename, track);
            mediaWriterVideo->start();
        } else if (codec == srtc::Codec::AV1) {
            mediaWriterVideo = std::make_shared<MediaWriterAV1>(gOutputVideoFilename, track);
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
    if (const auto offerSetError = peerConnection->setOffer(offer); offerSetError.isError()) {
        std::cout << "Error: cannot set offer: " << offerSetError.message << std::endl;
        exit(1);
    }

    if (const auto answerSetError = peerConnection->setAnswer(answer); answerSetError.isError()) {
        std::cout << "Error: cannot set answer: " << answerSetError.message << std::endl;
        exit(1);
    }

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
