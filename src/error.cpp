#include "srtc/error.h"

namespace srtc
{

const Error Error::OK = { Error::Code::OK, "OK" };

Error::Error(Code code, const std::string& message)
    : code(code)
    , message(message)
{
}

} // namespace srtc
