#pragma once

#include "media_reader.h"

class MediaReaderAV1 : public MediaReader
{
public:
    explicit MediaReaderAV1(const std::string& filename);
    ~MediaReaderAV1() override;

    [[nodiscard]] LoadedMedia loadMedia(bool print_info) const override;
};