#include "media_reader_vp8.h"
#include "media_reader_webm.h"

#include "srtc/util.h"

MediaReaderVP8::MediaReaderVP8(const std::string& filename)
    : MediaReader(filename)
{
}

MediaReaderVP8::~MediaReaderVP8() = default;

LoadedMedia MediaReaderVP8::loadMedia(bool print_info) const
{
    const auto data = loadFile();

    LoadedMedia loaded_media = {};
    loaded_media.codec = srtc::Codec::VP8;

    WebmLoader loader(data, "V_VP8", "VP8", loaded_media);

    loader.process();

    if (print_info) {
        loader.printInfo();
    }

    return loaded_media;
}
