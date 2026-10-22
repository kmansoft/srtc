#pragma once

#if defined SRTC_USE_OPENSSL
#include "crypto_util_openssl.h"
#elif defined SRTC_USE_MBEDTLS
#include "crypto_util_mbedtls.h"
#endif
