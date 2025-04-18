#include "srtc/peer_connection.h"
#include "srtc/sdp_offer.h"
#include "srtc/sdp_answer.h"

#include <curl/curl.h>
#include <curl/easy.h>

#include <iostream>
#include <memory>

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

    // Set the content type header to application/json
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/sdp");

    // Authorization header
    const auto authHeader = "Authorization: Bearer " + token;
    headers = curl_slist_append(headers, authHeader.c_str());

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Set up reading the response
    std::string answer;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, string_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &answer);

    // Perform the request
    const auto res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "Error: curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
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

static std::string gWhipUrl = "http://localhost:8080/whip";
static std::string gWhipToken = "none";

int main() {

    curl_global_init(CURL_GLOBAL_DEFAULT);

    using namespace srtc;

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

    const auto peerConnection = std::make_shared<PeerConnection>();

    std::mutex connectionStateMutex;
    PeerConnection::ConnectionState connectionState = PeerConnection::ConnectionState::Inactive;
    std::condition_variable connectionStateCond;

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

    std::this_thread::sleep_for(std::chrono::seconds(5));

    return 0;
}
