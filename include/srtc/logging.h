#pragma once

#include <cstdarg>

namespace srtc
{

#define SRTC_LOG_V 100
#define SRTC_LOG_I 101
#define SRTC_LOG_W 102
#define SRTC_LOG_E 103

#define SRTC_LOG_Z 1000

void setLogLevel(int level);

void log(int level, const char* tag, const char* format...)
#if defined(__clang__) || defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
;

void log_v(int level, const char* tag, const char* format, va_list ap);

} // namespace srtc
