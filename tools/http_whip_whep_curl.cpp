#include <curl/curl.h>
#include <curl/easy.h>

#include <algorithm>
#include <iostream>
#include <string>

namespace
{
bool gInitCurlDone = false;

void initCurl()
{
	if (!gInitCurlDone) {
		gInitCurlDone = true;
		curl_global_init(CURL_GLOBAL_DEFAULT);
	}
}
} // namespace

std::size_t string_write_callback(const char* in, size_t size, size_t nmemb, std::string* out)
{
	const auto total_size = size * nmemb;
	if (total_size) {
		out->append(in, total_size);
		return total_size;
	}
	return 0;
}

std::string perform_whip_whep(const std::string& offer, const std::string& url, const std::string& token)
{
	initCurl();

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

	// Remove \r chars
	answer.erase(std::remove(answer.begin(), answer.end(), '\r'), answer.end());

	return answer;
}
