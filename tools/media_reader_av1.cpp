#include "media_reader_av1.h"
#include "media_reader_webm.h"

#include "srtc/util.h"

MediaReaderAV1::MediaReaderAV1(const std::string& filename)
    : MediaReader(filename)
{
}

MediaReaderAV1::~MediaReaderAV1() = default;

LoadedMedia MediaReaderAV1::loadMedia(bool print_info) const
{
    const auto data = loadFile();

    LoadedMedia loaded_media = {};
    loaded_media.codec = srtc::Codec::AV1;

    WebmLoader loader(data, "V_AV1", "AV1", loaded_media);

    loader.process();

    if (print_info) {
        loader.printInfo();
    }

    return loaded_media;
}
