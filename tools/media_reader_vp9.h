#pragma once

#include "media_reader.h"

class MediaReaderVP9 final : public MediaReader
{
public:
    explicit MediaReaderVP9(const std::string& filename);
    ~MediaReaderVP9() override;

    [[nodiscard]] LoadedMedia loadMedia(bool print_info) const override;
};
