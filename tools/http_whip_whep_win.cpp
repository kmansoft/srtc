#include "srtc/srtc.h"

#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include <codecvt>
#include <cstdint>
#include <iostream>
#include <locale>
#include <string>

namespace
{

struct Url {
	bool is_secure;
	uint16_t port;

	std::string scheme;
	std::string server;
	std::string path;
};

void parseUrl(Url& url, const std::string& source)
{
	auto copy = source;

	// Scheme
	auto i = copy.find("://");
	if (i == std::string::npos) {
		std::cout << "Error: the url " << source << " should contain a scheme" << std::endl;
		exit(1);
	}

	url.scheme = copy.substr(0, i);
	copy = copy.substr(i + 3);

	if (copy.empty()) {
		std::cout << "Error: the url " << source << " is empty" << std::endl;
		exit(1);
	}

	url.is_secure = url.scheme == "https";

	// Path
	i = copy.find('/');
	if (i == std::string::npos) {
		url.path = "/";
	} else {
		url.path = copy.substr(i);
		copy = copy.substr(0, i);
	}

	// Server and port
	i = copy.find(':');
	if (i == std::string::npos) {
		if (url.is_secure) {
			url.port = 443;
		} else {
			url.port = 80;
		}
		url.server = copy;
	} else {
		const auto port_s = copy.substr(i + 1);
		if (port_s.empty()) {
			std::cout << "Error: the url " << source << " contains an empty port" << std::endl;
			exit(1);
		}

		const auto port_n = std::stoi(port_s);
		if (port_n <= 0 || port_n >= 65536) {
			std::cout << "Error: the url " << source << " contains an invalid port" << std::endl;
			exit(1);
		}

		url.port = port_n;
		url.server = copy.substr(0, i);
	}
}

class InternetHandle
{
public:
	InternetHandle(HINTERNET h)
		: mHandle(h)
	{
	}

	~InternetHandle()
	{
		WinHttpCloseHandle(mHandle);
	}

	HINTERNET mHandle;
};

std::wstring StringToWString(const std::string& str)
{
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
	return converter.from_bytes(str);
}

std::string WStringToString(const std::wstring& str)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	return converter.to_bytes(str);
}

} // namespace

std::string perform_whip_whep(const std::string& offer, const std::string& url, const std::string& token)
{
	Url urlobj;
	parseUrl(urlobj, url);

	const InternetHandle hSession = { WinHttpOpen(
		L"srtc", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
	if (!hSession.mHandle) {
		std::cout << "Error: cannot create a WinHttp session" << std::endl;
		exit(1);
	}

	unsigned int redirectCount = 0;

	DWORD dwRedirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
	WinHttpSetOption(hSession.mHandle, WINHTTP_OPTION_REDIRECT_POLICY, &dwRedirectPolicy, sizeof(dwRedirectPolicy));

	while (true) {
		const auto server = StringToWString(urlobj.server);
		const auto path = StringToWString(urlobj.path);

		// Connect to the server
		const InternetHandle hConnect = { WinHttpConnect(hSession.mHandle, server.data(), urlobj.port, 0) };
		if (!hConnect.mHandle) {
			std::cout << "Error: cannot connect to " << urlobj.server << std::endl;
			exit(1);
		}

		LPCWSTR accept[] = { L"application/sdp", nullptr };

		const InternetHandle hRequest = { WinHttpOpenRequest(hConnect.mHandle,
															 L"POST",
															 path.data(),
															 NULL,
															 WINHTTP_NO_REFERER,
															 accept,
															 urlobj.is_secure ? WINHTTP_FLAG_SECURE : 0) };
		if (!hRequest.mHandle) {
			std::cout << "Error: cannot create request for " << url << std::endl;
			exit(1);
		}

		const auto headers =
			StringToWString("Authorization: Bearer " + token + "\r\n" + "Content-Type: application/sdp\r\n");

		// Send request with POST data
		if (!WinHttpSendRequest(hRequest.mHandle,
								headers.data(),
								headers.size(),
								(LPVOID)offer.data(),
								offer.size(),
								offer.size(),
								0)) {
			std::cout << "Error: cannot send request to " << url << std::endl;
			exit(1);
		}

		// Receive response
		if (!WinHttpReceiveResponse(hRequest.mHandle, NULL)) {
			std::cout << "Error: cannot receive response from " << url << std::endl;
			exit(1);
		}

		// Check status code
		DWORD dwStatusCode = 0;
		DWORD dwStatusCodeSize = sizeof(dwStatusCode);

		if (!WinHttpQueryHeaders(hRequest.mHandle,
								 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
								 WINHTTP_HEADER_NAME_BY_INDEX,
								 &dwStatusCode,
								 &dwStatusCodeSize,
								 WINHTTP_NO_HEADER_INDEX)) {
			std::cout << "Error: cannot receive status code from " << url << std::endl;
			exit(1);
		}

		if (dwStatusCode == 200 || dwStatusCode == 201) {
			// We received a response
			std::string answer;
			while (true) {
				DWORD dwSize = 0;
				if (!WinHttpQueryDataAvailable(hRequest.mHandle, &dwSize) || dwSize == 0) {
					break;
				}

				std::unique_ptr<uint8_t[]> buf(new uint8_t[dwSize]);

				DWORD dwRead = 0;
				if (!WinHttpReadData(hRequest.mHandle, (LPVOID)buf.get(), dwSize, &dwRead) || dwRead == 0) {
					break;
				}

				answer.append(reinterpret_cast<const char*>(buf.get()), dwRead);
			}

			if (answer.empty()) {
				std::cout << "Error: received empty SDP" << std::endl;
				exit(1);
			}

			return answer;
		} else if (dwStatusCode == 301 || dwStatusCode == 302 || dwStatusCode == 307) {
			// We received a redirect, follow it
			if (++redirectCount >= 10) {
				std::cout << "Error: too many redirects" << std::endl;
				exit(1);
			}

			DWORD dwSize = 4096;
			std::unique_ptr<wchar_t[]> buf(new wchar_t[dwSize]);

			if (!WinHttpQueryHeaders(hRequest.mHandle,
									 WINHTTP_QUERY_LOCATION,
									 WINHTTP_HEADER_NAME_BY_INDEX,
									 buf.get(),
									 &dwSize,
									 WINHTTP_NO_HEADER_INDEX) ||
				dwSize == 0) {
				std::cout << "Error: no location header present on a redirect" << std::endl;
				exit(1);
			}

			const auto location = WStringToString(std::wstring(buf.get(), dwSize));
			if (location.find("://") != std::string::npos) {
				parseUrl(urlobj, location);
			} else if (!location.empty() && location[0] == '/') {
				urlobj.path = location;
			} else {
				std::cout << "Error: invalid location header " << location << std::endl;
				exit(1);
			}
		} else {
			// Something we don't understand
			std::cout << "Error: invalid status code " << dwStatusCode << std::endl;
			exit(1);
		}
	}
}