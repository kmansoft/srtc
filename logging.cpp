#include "srtc/logging.h"

#include <cstdarg>

#ifdef ANDROID
#include <android/log.h>
#include <cstdarg>
#else
#include <string>
#include <iostream>
#endif

namespace srtc {

void log(const char* tag,
         const char* format...)
{
    va_list ap;
    va_start(ap, format);

    log_v(tag, format, ap);

    va_end(ap);
}

void log_v(const char* tag,
           const char* format,
           va_list ap)
{
#ifdef ANDROID
    __android_log_vprint(ANDROID_LOG_INFO, tag, format, ap);
#else
    std::string buf;
    buf.reserve(4096);
    std::snprintf(buf.data(), buf.capacity(), format, ap);
    std::cout << tag << ": " << buf << std::endl;
#endif
}

}
