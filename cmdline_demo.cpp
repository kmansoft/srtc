#include "srtc/peer_connection.h"
#include "srtc/sdp_offer.h"
#include "srtc/sdp_answer.h"
#include "srtc/h264.h"

#include <curl/curl.h>
#include <curl/easy.h>

#include <iostream>
#include <memory>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

std::size_t string_write_callback(const char* in, size_t size, size_t nmemb, std::string* out) {
    const auto total_size = size * nmemb;
    if (total_size) {
        out->append(in, total_size);
        return total_size;
    }
    return 0;
}

std::string perform_whip(const std::string& offer,
                         const std::string& url,
                         const std::string& token) {
    const auto curl = curl_easy_init();
    if (!curl) {
        std::cout << "Error: cannot create a curl object" << std::endl;
        exit(1);
    }

    // Set the URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // Set the request type to POST
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    // Set the POST data
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, offer.c_str());

    // follow redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_UNRESTRICTED_AUTH, 1L);
    
    // Set the content type header to application/json
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/sdp");

    // Authorization header
    const auto authHeader = "Authorization: Bearer " + token;
    headers = curl_slist_append(headers, authHeader.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Redirect in case someone hacks the code to publish to IVS
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_UNRESTRICTED_AUTH, 1L);

    // Set up reading the response
    std::string answer;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, string_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &answer);

    // Perform the request
    const auto res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "Error: curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        exit(1);
    } else {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        if (response_code > 201) {
            std::cout << "Error: WHIP response code: " << response_code << std::endl;
            exit(1);
        }
    }

    // Clean up
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return answer;
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

srtc::ByteBuffer readInputFile(const std::string& fileName)
{
    struct stat statbuf;
    if (stat(fileName.c_str(), &statbuf) != 0) {
        std::cout << "*** Cannot stat input file " << fileName << std::endl;
        exit(1);
    }

    const auto h = open(fileName.c_str(), O_RDONLY);
    if (h < 0) {
        std::cout << "*** Cannot open input file " << fileName << std::endl;
        exit(1);
    }

    const auto sz = static_cast<size_t>(statbuf.st_size);

    srtc::ByteBuffer buf(sz);
    buf.resize(sz);

    if (read(h, buf.data(), sz) != sz) {
        std::cout << "*** Cannot read input file " << fileName << std::endl;
        exit(1);
    }

    close(h);

    size_t frame_n = 0;
    for (srtc::h264::NaluParser parser(buf); parser; parser.next()) {
        const auto naluType = parser.currType();
        switch (naluType) {
            case srtc::h264::NaluType::SPS:
                std::cout << "--- " << frame_n << " SPS" << std::endl;
                break;
            case srtc::h264::NaluType::PPS:
                std::cout << "--- " << frame_n << " PPS" << std::endl;
                break;
            case srtc::h264::NaluType::KeyFrame:
                std::cout << "--- " << frame_n << " KeyFrame" << std::endl;
            default:
                break;
        }
        frame_n += 1;
    }

    return std::move(buf);
}

void playVideoFile(const std::shared_ptr<srtc::PeerConnection>& peerConnection,
                   const srtc::ByteBuffer& data)
{
    std::vector<srtc::ByteBuffer> codecSpecificData;

    // Iterate other frames
    uint32_t frameCount = 0;

    for (srtc::h264::NaluParser parser(data); parser; parser.next()) {
        const auto naluType = parser.currType();
        switch (naluType) {
            case srtc::h264::NaluType::SPS:
            case srtc::h264::NaluType::PPS:
                peerConnection->publishVideoSingleFrame({ parser.currNalu(), parser.currNaluSize() });
                break;
            case srtc::h264::NaluType::KeyFrame:
            case srtc::h264::NaluType::NonKeyFrame:
                peerConnection->publishVideoSingleFrame({ parser.currNalu(), parser.currNaluSize() });
                usleep(1000 * 40);  // 25 fps
                break;
            default:
                break;
        }

        frameCount += 1;
    }

    std::cout << "Played " << frameCount << " frames" << std::endl;
}

static std::string gInputFile = "sintel.h264";
static std::string gWhipUrl = "http://localhost:8080/whip";
static std::string gWhipToken = "none";

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -f, --file <path>    Path to H.264 file (default: " << gInputFile << ")" << std::endl;
    std::cout << "  -u, --url <url>      WHIP server URL (default: " << gWhipUrl << ")" << std::endl;
    std::cout << "  -t, --token <token>  WHIP authorization token" << std::endl;
    std::cout << "  -h, --help           Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
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
                gWhipToken = argv[++i];
            } else {
                std::cerr << "Error: -t/--token requires a token value" << std::endl;
                return 1;
            }
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    std::cout << "Using H.264 file: " << gInputFile << std::endl;
    std::cout << "Using WHIP URL: " << gWhipUrl << std::endl;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    using namespace srtc;

    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
        std::cout << "*** Cannot get current working directory" << std::endl;
        exit(1);
    }

    std::cout << "*** Current working directory: " << cwd << std::endl;

    // Read the file
    const auto inputFileData = readInputFile(gInputFile);
    std::cout << "*** Read " << inputFileData.size() << " bytes from input video file" << std::endl;

    // Offer

    const OfferConfig offerConfig = {
            .cname = "foo"
    };
    const PubVideoConfig videoConfig = {
            .codecList = {
                { Codec::H264, 0x42e01f}
            }
    };

    const auto offer = std::make_shared<SdpOffer>(offerConfig, videoConfig, nullopt);
    const auto [ offerString, offerError ] = offer->generate();
    if (offerError.isError()) {
        std::cout << "Error: cannot generate offer: " << offerError.mMessage << std::endl;
        exit(1);
    }

    // WHIP

    const auto answerString = perform_whip(offerString, gWhipUrl, gWhipToken);

    // Answer

    const auto [ answer, answerError ] = SdpAnswer::parse(offer, answerString, nullptr);
    if (answerError.isError()) {
        std::cout << "Error: cannot parse answer: " << answerError.mMessage << std::endl;
        exit(1);
    }

    // Peer connection

    std::mutex connectionStateMutex;
    PeerConnection::ConnectionState connectionState = PeerConnection::ConnectionState::Inactive;
    std::condition_variable connectionStateCond;

    const auto peerConnection = std::make_shared<PeerConnection>();

    peerConnection->setConnectionStateListener(
            [&connectionStateMutex, &connectionState, &connectionStateCond](const PeerConnection::ConnectionState& state) {
                std::cout << "*** PeerConnection state: " << connectionStateToString(state) << std::endl;

                {
                    std::lock_guard lock(connectionStateMutex);
                    connectionState = state;
                }
                connectionStateCond.notify_one();
    });

    peerConnection->setSdpOffer(offer);
    peerConnection->setSdpAnswer(answer);

    // Wait for connection to either be connected or fail

    {
        std::unique_lock lock(connectionStateMutex);
        connectionStateCond.wait_for(lock, std::chrono::seconds(15), [&connectionState]() {
            return
                connectionState == PeerConnection::ConnectionState::Connected ||
                connectionState == PeerConnection::ConnectionState::Failed;
        });

        if (connectionState == PeerConnection::ConnectionState::Failed) {
            std::cout << "*** Failed to connect" << std::endl;
            exit(1);
        }
    }

    // Play the video

    playVideoFile(peerConnection, inputFileData);

    // Wait a little and exit

    std::this_thread::sleep_for(std::chrono::seconds(1));

    return 0;
}
