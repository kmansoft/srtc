#include "srtc/srtp_openssl.h"

#include <mutex>

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace {

std::once_flag gInitFlag;

}

namespace srtc {

void initOpenSSL() {
    std::call_once(gInitFlag, []{
        OpenSSL_add_all_algorithms();
        OpenSSL_add_all_ciphers();
        OpenSSL_add_all_digests();
        ERR_load_crypto_strings();
    });
}

}

