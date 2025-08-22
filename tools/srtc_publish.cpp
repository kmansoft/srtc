#include "srtc/h264.h"
#include "srtc/logging.h"
#include "srtc/peer_connection.h"
#include "srtc/sdp_answer.h"
#include "srtc/sdp_offer.h"
#include "srtc/util.h"

#include "http_whip_whep.h"
#include "media_reader.h"

#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

// Program options

static std::string gInputFile = "sintel.h264";
static std::string gWhipUrl = "http://localhost:8080/whip";
static std::string gAuthToken = "none";
static bool gQuiet = false;
static bool gPrintSDP = false;
static bool gPrintInfo = false;
static bool gDropPackets = false;
static bool gEnableBWE = false;
static bool gLoopVideo = false;

// State

static std::atomic_bool gIsConnectionFailed = false;

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

void playVideoFile(const std::shared_ptr<srtc::PeerConnection>& peerConnection, const LoadedMedia& media)
{
    std::optional<int64_t> pts_usec;

    while (true) {
        uint32_t frame_count = 0;

        for (const auto& frame : media.frame_list) {
            if (pts_usec.has_value()) {
                const auto delta_usec = frame.pts_usec - pts_usec.value();
                std::this_thread::sleep_for(std::chrono::microseconds(delta_usec));
            }
            pts_usec = frame.pts_usec;

            if (!frame.csd.empty()) {
                std::vector<srtc::ByteBuffer> csd_copy(frame.csd.size());
                for (const auto& item : frame.csd) {
                    csd_copy.push_back(item.copy());
                }

                peerConnection->setVideoSingleCodecSpecificData(std::move(csd_copy));
            }

            peerConnection->publishVideoSingleFrame(frame.pts_usec, frame.frame.copy());

            frame_count += 1;

            if (!gQuiet && frame_count > 0 && (frame_count % 25) == 0) {
                std::cout << "Played " << std::setw(5) << frame_count << " video frames" << std::endl;
            }

            if (gIsConnectionFailed) {
                std::cout << "*** Connection failed, stopping video playback" << std::endl;
                return;
            }
        }

        if (gLoopVideo) {
            std::cout << "Looping back to the beginning" << std::endl;
        } else {
            break;
        }
    }
}

void printUsage(const char* programName)
{
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -f, --file <path>    Path to H.264 file (default: " << gInputFile << ")" << std::endl;
    std::cout << "  -u, --url <url>      WHIP server URL (default: " << gWhipUrl << ")" << std::endl;
    std::cout << "  -t, --token <token>  WHIP authorization token" << std::endl;
    std::cout << "  -l, --loop           Loop the file" << std::endl;
    std::cout << "  -v, --verbose        Verbose logging from the srtc library" << std::endl;
    std::cout << "  -q, --quiet          Suppress progress reporting" << std::endl;
    std::cout << "  -s, --sdp            Print SDP offer and answer" << std::endl;
    std::cout << "  -i, --info           Print input file info" << std::endl;
    std::cout << "  -d, --drop           Drop some packets at random (test NACK and RTX handling)" << std::endl;
    std::cout << "  -b, --bwe            Enable TWCC congestion control for bandwidth estimation" << std::endl;
    std::cout << "  -h, --help           Show this help message" << std::endl;
}

int main(int argc, char* argv[])
{
    // Set logging to errors by default
    srtc::setLogLevel(SRTC_LOG_W);

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-f" || arg == "--file") {
            if (i + 1 < argc) {
                gInputFile = argv[++i];
            } else {
                std::cerr << "Error: -f/--file requires a file path" << std::endl;
                return 1;
            }
        } else if (arg == "-u" || arg == "--url") {
            if (i + 1 < argc) {
                gWhipUrl = argv[++i];
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
        } else if (arg == "-l" || arg == "--loop") {
            gLoopVideo = true;
        } else if (arg == "-v" || arg == "--verbose") {
            srtc::setLogLevel(SRTC_LOG_V);
        } else if (arg == "-q" || arg == "--quiet") {
            gQuiet = true;
        } else if (arg == "-s" || arg == "--sdp") {
            gPrintSDP = true;
        } else if (arg == "-i" || arg == "--info") {
            gPrintInfo = true;
        } else if (arg == "-d" || arg == "--drop") {
            gDropPackets = true;
        } else if (arg == "-b" || arg == "--bwe") {
            gEnableBWE = true;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    std::cout << "*** Using source file: " << gInputFile << std::endl;
    std::cout << "*** Using WHIP URL:    " << gWhipUrl << std::endl;

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

    // Read the file
    const auto media_reader = MediaReader::create(gInputFile);
    const auto media_file = media_reader->loadMedia(gPrintInfo);

    // Peer connection state
    std::mutex connectionStateMutex;
    PeerConnection::ConnectionState connectionState = PeerConnection::ConnectionState::Inactive;
    std::condition_variable connectionStateCond;

    // Peer connection
    auto connectedReported = false;
    const auto ms0 = std::chrono::steady_clock::now();
    const auto peerConnection = std::make_shared<PeerConnection>(Direction::Publish);

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

    peerConnection->setPublishConnectionStatsListener([](const PublishConnectionStats& stats) {
        std::cout << "*** PeerConnection stats: sent " << stats.packet_count << " packets, " << stats.byte_count
                  << " bytes, act " << std::setprecision(6) << stats.bandwidth_actual_kbit_per_second << " kb/s, sugg "
                  << std::setprecision(6) << stats.bandwidth_suggested_kbit_per_second << " kb/s, "
                  << std::setprecision(3) << stats.packets_lost_percent << "% packet loss, " << std::setprecision(3)
                  << stats.rtt_ms << " ms rtt" << std::endl;
    });

    // Offer
    PubOfferConfig offer_config = {};
    offer_config.cname = "foo";
    offer_config.enable_rtx = true;
    offer_config.enable_bwe = gEnableBWE;
    offer_config.debug_drop_packets = gDropPackets;

    PubVideoCodec video_codec = {};
    video_codec.codec = media_file.codec;
    if (video_codec.codec == srtc::Codec::H264) {
        video_codec.profile_level_id = 0x42e01f;
    }

    PubVideoConfig video_config = {};
    video_config.codec_list.push_back(video_codec);

    const auto [offer, offerCreateError] = peerConnection->createPublishOffer(offer_config, video_config, std::nullopt);
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

    // WHIP
    const auto answerString = perform_whip_whep(offerString, gWhipUrl, gAuthToken);
    if (gPrintSDP) {
        std::cout << "----- SDP answer -----\n" << answerString << std::endl;
    }

    // Answer
    const auto [answer, answerError] = peerConnection->parsePublishAnswer(offer, answerString, nullptr);
    if (answerError.isError()) {
        std::cout << "Error: cannot parse answer: " << answerError.message << std::endl;
        exit(1);
    }

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

    // Play the video
    playVideoFile(peerConnection, media_file);

    // Wait a little and exit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    peerConnection->close();

    return 0;
}
