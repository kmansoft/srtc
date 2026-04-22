#include "media_reader_vp9.h"
#include "media_reader_webm.h"

MediaReaderVP9::MediaReaderVP9(const std::string& filename)
    : MediaReader(filename)
{
}

MediaReaderVP9::~MediaReaderVP9() = default;

LoadedMedia MediaReaderVP9::loadMedia(bool print_info) const
{
    const auto data = loadFile();

    LoadedMedia loaded_media = {};
    loaded_media.codec = srtc::Codec::VP9;

    WebmLoader loader(data, "V_VP9", "VP9", loaded_media);

    loader.process();

    if (print_info) {
        loader.printInfo();
    }

    return loaded_media;
}
