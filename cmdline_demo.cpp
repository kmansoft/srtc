#include "srtc/h264.h"
#include "srtc/logging.h"
#include "srtc/peer_connection.h"
#include "srtc/sdp_answer.h"
#include "srtc/sdp_offer.h"

#include <curl/curl.h>
#include <curl/easy.h>

#include <iomanip>
#include <iostream>
#include <memory>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// Program options

static std::string gInputFile = "sintel.h264";
static std::string gWhipUrl = "http://localhost:8080/whip";
static std::string gWhipToken = "none";
static bool gQuiet = false;
static bool gPrintSDP = false;
static bool gPrintInfo = false;
static bool gDropPackets = false;
static bool gEnableBWE = false;
static bool gLoopVideo = false;

// State

static std::atomic_bool gIsConnectionFailed = false;

// Bit reader for determining frame boundaries

class BitReader
{
private:
	const uint8_t* const data;
	const size_t dataSize;
	size_t bitPos;

public:
	BitReader(const uint8_t* buffer, size_t size)
		: data(buffer)
		, dataSize(size)
		, bitPos(0)
	{
	}

	uint32_t readBit();
	uint32_t readBits(size_t n);
	uint32_t readUnsignedExpGolomb();
};

uint32_t BitReader::readBit()
{
	if ((bitPos >> 3) >= dataSize)
		return 0;

	uint8_t byte = data[bitPos >> 3];
	uint32_t bit = (byte >> (7 - (bitPos & 7))) & 1;
	bitPos++;
	return bit;
}

uint32_t BitReader::readBits(size_t n)
{
	uint32_t value = 0;
	for (size_t i = 0; i < n; i++) {
		value = (value << 1) | readBit();
	}
	return value;
}

uint32_t BitReader::readUnsignedExpGolomb()
{
	// Count leading zeros
	int leadingZeros = 0;
	while (readBit() == 0 && leadingZeros < 32) {
		leadingZeros++;
	}

	if (leadingZeros == 0)
		return 0;

	// Read remaining bits
	uint32_t remainingBits = readBits(leadingZeros);
	return (1 << leadingZeros) - 1 + remainingBits;
}

// WHIP

std::size_t string_write_callback(const char* in, size_t size, size_t nmemb, std::string* out)
{
	const auto total_size = size * nmemb;
	if (total_size) {
		out->append(in, total_size);
		return total_size;
	}
	return 0;
}

std::string perform_whip(const std::string& offer, const std::string& url, const std::string& token)
{
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

	return std::move(buf);
}

void printFileInfo(const srtc::ByteBuffer& data)
{
	uint32_t naluCount = 0;
	uint32_t parameterCount = 0;
	uint32_t frameCount = 0;

	for (srtc::h264::NaluParser parser(data); parser; parser.next()) {
		const auto naluType = parser.currType();

		switch (naluType) {
		default:
			break;
		case srtc::h264::NaluType::KeyFrame:
		case srtc::h264::NaluType::NonKeyFrame:
			naluCount += 1;
			break;
		case srtc::h264::NaluType::SPS:
		case srtc::h264::NaluType::PPS:
			parameterCount += 1;
			break;
		}

		switch (naluType) {
		default:
			break;
		case srtc::h264::NaluType::KeyFrame:
		case srtc::h264::NaluType::NonKeyFrame:
			BitReader br = { parser.currData() + 1, parser.currDataSize() - 1 };
			const auto first_mb_in_slice = br.readUnsignedExpGolomb();
			if (first_mb_in_slice == 0) {
				frameCount += 1;
			}
			break;
		}
	}

	std::cout << "*** NALU count:      " << std::setw(4) << naluCount << std::endl;
	std::cout << "*** Parameter count: " << std::setw(4) << parameterCount << std::endl;
	std::cout << "*** Frame count:     " << std::setw(4) << frameCount << std::endl;
}

void playVideoFile(const std::shared_ptr<srtc::PeerConnection>& peerConnection, const srtc::ByteBuffer& data)
{
	while (true) {
		std::vector<srtc::ByteBuffer> codecSpecificData;

		// Iterate other nalus
		uint32_t naluCount = 0;
		uint32_t frameCount = 0;

		srtc::ByteBuffer sps, pps;
		srtc::ByteBuffer frame;

		for (srtc::h264::NaluParser parser(data); parser; parser.next()) {
			const auto naluType = parser.currType();
			switch (naluType) {
			default:
				break;
			case srtc::h264::NaluType::SPS:
				sps.assign(parser.currNalu(), parser.currNaluSize());
				break;
			case srtc::h264::NaluType::PPS:
				pps.assign(parser.currNalu(), parser.currNaluSize());
				break;
			case srtc::h264::NaluType::KeyFrame:
			case srtc::h264::NaluType::NonKeyFrame:
				BitReader br = { parser.currData() + 1, parser.currDataSize() - 1 };
				const auto first_mb_in_slice = br.readUnsignedExpGolomb();
				if (first_mb_in_slice == 0) {
					if (naluType == srtc::h264::NaluType::KeyFrame) {
						std::vector<srtc::ByteBuffer> parameters;
						parameters.push_back(sps.copy());
						parameters.push_back(pps.copy());
						peerConnection->setVideoSingleCodecSpecificData(std::move(parameters));
					}

					if (!frame.empty()) {
						peerConnection->publishVideoSingleFrame(std::move(frame));
						frame.clear();
					}
					frameCount += 1;
					usleep(1000 * 40); // 25 fps
				}
				frame.append(parser.currNalu(), parser.currNaluSize());
				naluCount += 1;
				break;
			}

			if (!gQuiet && frameCount > 0 && (frameCount % 25) == 0) {
				std::cout << "Played " << std::setw(5) << naluCount << " nalus" << std::setw(5) << frameCount
						  << " video frames" << std::endl;
			}

			if (gIsConnectionFailed) {
				std::cout << "*** Connection failed, stopping video playback" << std::endl;
				return;
			}
		}

		if (!frame.empty()) {
			peerConnection->publishVideoSingleFrame(std::move(frame));
			frame.clear();
		}

		if (!gQuiet && frameCount > 0 && (frameCount % 25) != 0) {
			std::cout << "Played " << std::setw(5) << naluCount << " nalus" << std::setw(5) << frameCount
					  << " video frames" << std::endl;
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
	std::cout << "  -d, --drop           Drop some packets at random (test NCK and RTX handling)" << std::endl;
	std::cout << "  -b, --bwe            Enable Google's TWCC congestion control for bandwidth estimation" << std::endl;
	std::cout << "  -h, --help           Show this help message" << std::endl;
}

int main(int argc, char* argv[])
{
	// Set logging to errors by default
	srtc::setLogLevel(SRTC_LOG_E);

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

	std::cout << "*** Using H.264 file: " << gInputFile << std::endl;
	std::cout << "*** Using WHIP URL: " << gWhipUrl << std::endl;

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
	std::cout << "*** Read " << inputFileData.size() << " bytes from input video file " << gInputFile << std::endl;

	// Print file info
	if (gPrintInfo) {
		printFileInfo(inputFileData);
	}

	// Peer connection state
	std::mutex connectionStateMutex;
	PeerConnection::ConnectionState connectionState = PeerConnection::ConnectionState::Inactive;
	std::condition_variable connectionStateCond;

	// Peer connection
	const auto peerConnection = std::make_shared<PeerConnection>();

	peerConnection->setConnectionStateListener(
		[&connectionStateMutex, &connectionState, &connectionStateCond](const PeerConnection::ConnectionState& state) {
			std::cout << "*** PeerConnection state: " << connectionStateToString(state) << std::endl;

			if (state == PeerConnection::ConnectionState::Failed) {
				gIsConnectionFailed = true;
			}

			{
				std::lock_guard lock(connectionStateMutex);
				connectionState = state;
			}
			connectionStateCond.notify_one();
		});

	peerConnection->setPublishConnectionStatListener([](const PublishConnectionStats& stats) {
		std::cout << "*** PeerConnection stats: sent " << stats.packet_count << " packets, " << stats.byte_count
				  << " bytes, " << std::setprecision(3) << stats.packets_lost_percent << "% packet loss, "
				  << stats.rtt_ms << " ms rtt" << std::endl;
	});

	// Offer
	const OfferConfig offerConfig = {
		.cname = "foo", .enable_rtx = true, .enable_bwe = gEnableBWE, .debug_drop_packets = gDropPackets
	};
	const PubVideoConfig videoConfig = { .codec_list = { { .codec = Codec::H264, .profile_level_id = 0x42e01f } } };

	const auto offer = peerConnection->createPublishSdpOffer(offerConfig, videoConfig, std::nullopt);
	const auto [offerString, offerError] = offer->generate();
	if (offerError.isError()) {
		std::cout << "Error: cannot generate offer: " << offerError.mMessage << std::endl;
		exit(1);
	}
	if (gPrintSDP) {
		std::cout << "----- SDP offer -----\n" << offerString << std::endl;
	}

	// WHIP
	const auto answerString = perform_whip(offerString, gWhipUrl, gWhipToken);
	if (gPrintSDP) {
		std::cout << "----- SDP answer -----\n" << answerString << std::endl;
	}

	// Answer
	const auto [answer, answerError] = peerConnection->parsePublishSdpAnswer(offer, answerString, nullptr);
	if (answerError.isError()) {
		std::cout << "Error: cannot parse answer: " << answerError.mMessage << std::endl;
		exit(1);
	}

	// Connect the peer connection
	peerConnection->setSdpOffer(offer);
	peerConnection->setSdpAnswer(answer);

	// Wait for connection to either be connected or fail
	{
		std::unique_lock lock(connectionStateMutex);
		connectionStateCond.wait_for(lock, std::chrono::seconds(15), [&connectionState]() {
			return connectionState == PeerConnection::ConnectionState::Connected ||
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
