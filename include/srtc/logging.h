#pragma once

#include <cstdarg>

namespace srtc {

void log(const char* tag,
         const char* format...)
        __attribute__ ((format (printf, 2, 3)));

void log_v(const char* tag,
           const char* format,
           va_list ap);

}
