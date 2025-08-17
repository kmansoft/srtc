#pragma once

#include <string>

namespace srtc
{

class Error
{
public:
	enum class Code {
		OK,
		InvalidData
	};

	[[nodiscard]] bool isOk() const
	{
		return code == Code::OK;
	}

	[[nodiscard]] bool isError() const
	{
		return code != Code::OK;
	}

	static const Error OK;

	Error(Code code, const std::string& message);

	const Code code;
	const std::string message;
};

} // namespace srtc
