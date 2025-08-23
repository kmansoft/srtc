#pragma once

#include "media_reader.h"

class MediaReaderH264 : public MediaReader
{
public:
    explicit MediaReaderH264(const std::string& filename);
    ~MediaReaderH264() override;

    [[nodiscard]] LoadedMedia loadMedia(bool print_info) const override;

private:
    void printInfo(const srtc::ByteBuffer& buf) const;
};