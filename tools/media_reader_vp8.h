#pragma once

#include "media_reader.h"

class MediaReaderVP8 : public MediaReader
{
public:
    explicit MediaReaderVP8(const std::string& filename);
    ~MediaReaderVP8() override;

    [[nodiscard]] LoadedMedia loadMedia(bool print_info) const override;
};