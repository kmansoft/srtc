#include "srtc/logging.h"

#include <cstdarg>

#ifdef ANDROID
#include <android/log.h>
#include <cstdarg>
#else
#include <iostream>
#endif

namespace {

int gLogLevel =
#ifdef NDEBUG
        SRTC_LOG_E
#else
        SRTC_LOG_V
#endif
;

}

namespace srtc {

void setLogLevel(int level)
{
    gLogLevel = level;
}

void log(int level,
         const char* tag,
         const char* format...)
{
    va_list ap;
    va_start(ap, format);

    log_v(level, tag, format, ap);

    va_end(ap);
}

void log_v(int level,
           const char* tag,
           const char* format,
           va_list ap)
{
    if (level >= gLogLevel) {
#ifdef ANDROID
        int androidLogLevel;
        switch (level) {
            case SRTC_LOG_V:
                androidLogLevel = ANDROID_LOG_VERBOSE;
                break;
            default:
            case SRTC_LOG_I:
                androidLogLevel = ANDROID_LOG_INFO;
                break;
            case SRTC_LOG_E:
                androidLogLevel = ANDROID_LOG_ERROR;
                break;
        }

        __android_log_vprint(androidLogLevel, tag, format, ap);
#else
        char buf[2048];
        std::vsnprintf(buf, sizeof(buf), format, ap);
        std::cout << tag << ": " << buf << std::endl;
#endif
    }
}

}
