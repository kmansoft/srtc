#include "media_reader.h"
#include "media_reader_h264.h"

#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

MediaReader::MediaReader(const std::string& filename)
    : mFileName(filename)
{
}

MediaReader::~MediaReader() = default;

std::shared_ptr<MediaReader> MediaReader::create(const std::string& filename)
{
    const char sep =
#ifdef _WIN32
        '/';
#else
        '\\';
#endif

    const auto index_ext = filename.rfind('.');
    const auto index_sep = filename.rfind('/');

    std::string ext;
    if (index_ext != std::string::npos) {
        if (index_sep == std::string::npos || index_sep < index_ext) {
            ext = filename.substr(index_ext);
        }
    }

    if (ext == ".h264") {
        return std::make_shared<MediaReaderH264>(filename);
    }

    std::cout << "*** Cannot determine media type for " << filename << std::endl;
    exit(1);
}

srtc::ByteBuffer MediaReader::loadFile() const
{
    struct stat statbuf = {};
    if (stat(mFileName.c_str(), &statbuf) != 0) {
        std::cout << "*** Cannot stat input file " << mFileName << std::endl;
        exit(1);
    }

    const auto sz = static_cast<size_t>(statbuf.st_size);

    srtc::ByteBuffer buf(sz);
    buf.resize(sz);

#ifdef _WIN32
    const auto h =
        CreateFileA(fileName.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        std::cout << "*** Cannot open input file " << fileName << std::endl;
        exit(1);
    }

    DWORD bytesRead = {};
    if (!ReadFile(h, buf.data(), sz, &bytesRead, NULL) || bytesRead != sz) {
        std::cout << "*** Cannot read input file " << fileName << std::endl;
        exit(1);
    }

    CloseHandle(h);
#else
    const auto h = open(mFileName.c_str(), O_RDONLY);
    if (h < 0) {
        std::cout << "*** Cannot open input file " << mFileName << std::endl;
        exit(1);
    }

    if (read(h, buf.data(), sz) != sz) {
        std::cout << "*** Cannot read input file " << mFileName << std::endl;
        exit(1);
    }

    close(h);
#endif

    return std::move(buf);
}