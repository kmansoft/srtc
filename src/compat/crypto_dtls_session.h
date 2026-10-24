#pragma once

#if defined SRTC_USE_OPENSSL
#include "crypto_dtls_session_openssl.h"
#elif defined SRTC_USE_MBEDTLS
#include "crypto_dtls_session_mbedtls.h"
#else
#error "Unsupported / invalid crypto backend"
#endif
