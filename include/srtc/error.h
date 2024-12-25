#pragma once

#include <string>

namespace srtc {

class Error {
public:
    enum class Code {
        OK,
        InvalidData
    };

    [[nodiscard]] bool isOk() const {
        return mCode == Code::OK;
    }

    [[nodiscard]] bool isError() const {
        return mCode != Code::OK;
    }

    static const Error OK;

    Error(Code code, const std::string& message);

    const Code mCode;
    const std::string mMessage;
};

}
