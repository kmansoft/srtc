#include "srtc/logging.h"

#include <cstdarg>

#ifdef ANDROID
#include <android/log.h>
#include <cstdarg>
#else
#include <chrono>
#include <iomanip>
#include <iostream>
#endif

namespace
{

int gLogLevel =
#ifdef NDEBUG
    SRTC_LOG_E
#else
    SRTC_LOG_V
#endif
    ;

} // namespace

namespace srtc
{

void setLogLevel(int level)
{
    gLogLevel = level;
}

void log(int level, const char* tag, const char* format...)
{
    va_list ap;
    va_start(ap, format);

    log_v(level, tag, format, ap);

    va_end(ap);
}

void log_v(int level, const char* tag, const char* format, va_list ap)
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
        const auto now = std::chrono::system_clock::now();
        const auto time_t_now = std::chrono::system_clock::to_time_t(now);
        const auto local_tm = std::localtime(&time_t_now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

        char buf[2048];
        std::vsnprintf(buf, sizeof(buf), format, ap);
        std::cout << std::put_time(local_tm, "%Y-%m-%d %H:%M:%S") << "." << std::setw(3) << std::setfill('0') << ms
                  << " " << tag << ": " << buf << std::endl;
#endif
    }
}

} // namespace srtc
