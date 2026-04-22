#pragma once

#include "media_reader.h"

class MediaReaderH265 final : public MediaReader
{
public:
    explicit MediaReaderH265(const std::string& filename);
    ~MediaReaderH265() override;

    [[nodiscard]] LoadedMedia loadMedia(bool print_info) const override;

private:
    void printInfo(const srtc::ByteBuffer& buf) const;
};