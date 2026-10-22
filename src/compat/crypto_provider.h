#pragma once

#if defined SRTC_USE_OPENSSL
#include "crypto_provider_openssl.h"
#elif defined SRTC_USE_MBEDTLS
#include "crypto_provider_mbedtls.h"
#endif
