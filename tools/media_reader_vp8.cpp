#include "media_reader_vp8.h"

#include "srtc/util.h"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>

/////

class WebmReader
{
public:
    WebmReader(const uint8_t* data, size_t size);

    [[nodiscard]] size_t position() const;
    [[nodiscard]] size_t remaining() const;
    [[nodiscard]] const uint8_t* curr() const;

    void readBlockHeader(uint32_t& id, uint64_t& size);
    void skip(uint64_t size);

    [[nodiscard]] uint32_t readUInt(uint64_t size);
    [[nodiscard]] std::string readString(uint64_t size);

    [[nodiscard]] uint32_t readVInt32();
    [[nodiscard]] uint64_t readVInt64();

    [[nodiscard]] int16_t readFixedInt16();
    [[nodiscard]] uint8_t readFixedUInt8();

private:
    const uint8_t* const mData;
    const size_t mSize;
    size_t mPos;

    [[nodiscard]] uint32_t readID();
    [[nodiscard]] uint64_t readVInt();

    [[nodiscard]] uint64_t readVIntImpl(bool remove_marker);
};

WebmReader::WebmReader(const uint8_t* data, size_t size)
    : mData(data)
    , mSize(size)
    , mPos(0)
{
}

size_t WebmReader::position() const
{
    return mPos;
}

size_t WebmReader::remaining() const
{
    return mSize - mPos;
}

const uint8_t* WebmReader::curr() const
{
    return mData + mPos;
}

void WebmReader::readBlockHeader(uint32_t& id, uint64_t& size)
{
    if (remaining() < 1) {
        std::cout << "*** Attempt to read past end of webm block" << std::endl;
        exit(1);
    }

    // Read the ID
    id = readID();
    size = readVInt();
}

void WebmReader::skip(uint64_t size)
{
    if (remaining() < size) {
        std::cout << "*** Attempt to skip past end of webm block" << std::endl;
        exit(1);
    }

    mPos += size;
}

uint32_t WebmReader::readUInt(uint64_t size)
{
    if (size > 4 || remaining() < size) {
        std::cout << "*** Attempt to read a uint of invalid size from a webm block" << std::endl;
        exit(1);
    }

    uint8_t b[4] = {};
    std::memcpy(b + 4 - size, mData + mPos, size);
    mPos += size;

    return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}

std::string WebmReader::readString(uint64_t size)
{
    if (remaining() < size) {
        std::cout << "*** Attempt to read a string of invalid size from a webm block" << std::endl;
        exit(1);
    }

    std::string r;
    r.resize(size);

    std::memcpy(r.data(), mData + mPos, size);
    mPos += size;

    return r;
}

uint32_t WebmReader::readVInt32()
{
    return static_cast<uint32_t>(readVIntImpl(true));
}

uint64_t WebmReader::readVInt64()
{
    return readVIntImpl(true);
}

int16_t WebmReader::readFixedInt16()
{
    if (remaining() < 2) {
        std::cout << "*** Attempt to read a fixed int16 value past end of webm block" << std::endl;
        exit(1);
    }

    uint8_t b[2] = {};
    std::memcpy(b, mData + mPos, 2);
    mPos += 2;

    return static_cast<int16_t>((b[0] << 8) | b[1]);
}

uint8_t WebmReader::readFixedUInt8()
{
    if (remaining() < 1) {
        std::cout << "*** Attempt to read a fixed uint8 value past end of webm block" << std::endl;
        exit(1);
    }

    uint8_t b[1] = {};
    std::memcpy(b, mData + mPos, 1);
    mPos += 1;

    return b[0];
}

uint32_t WebmReader::readID()
{
    return readVIntImpl(false);
}

uint64_t WebmReader::readVInt()
{
    return readVIntImpl(true);
}

uint64_t WebmReader::readVIntImpl(bool remove_marker)
{
    if (remaining() < 1) {
        std::cout << "*** Attempt to read a value past end of webm block" << std::endl;
        exit(1);
    }

    uint64_t id_byte_0 = mData[mPos++];
    if ((id_byte_0 & 0x80) == 0x80) {
        // Single byte
        if (remove_marker) {
            id_byte_0 &= ~0x80;
        }
        return id_byte_0;
    } else if ((id_byte_0 & 0xC0) == 0x40) {
        // Two byte
        if (remaining() < 1) {
            std::cout << "*** Attempt to read a two byte value past end of webm block" << std::endl;
            exit(1);
        }
        if (remove_marker) {
            id_byte_0 &= ~0xC0;
        }
        const auto id_byte_1 = mData[mPos++];
        return (id_byte_0 << 8) | id_byte_1;
    } else if ((id_byte_0 & 0xE0) == 0x20) {
        // Three byte
        if (remaining() < 2) {
            std::cout << "*** Attempt to read a three byte value past end of webm block" << std::endl;
            exit(1);
        }
        if (remove_marker) {
            id_byte_0 &= ~0xE0;
        }
        const auto id_byte_1 = mData[mPos++];
        const auto id_byte_2 = mData[mPos++];
        return (id_byte_0 << 16) | (id_byte_1 << 8) | id_byte_2;
    } else if ((id_byte_0 & 0xF0) == 0x10) {
        // Four byte
        if (remaining() < 3) {
            std::cout << "*** Attempt to read a four byte value past end of webm block" << std::endl;
            exit(1);
        }
        if (remove_marker) {
            id_byte_0 &= ~0xF0;
        }
        const auto id_byte_1 = mData[mPos++];
        const auto id_byte_2 = mData[mPos++];
        const auto id_byte_3 = mData[mPos++];
        return (id_byte_0 << 24) | (id_byte_1 << 16) | (id_byte_2 << 8) | id_byte_3;
    } else if (id_byte_0 == 1) {
        // Eight byte
        if (remaining() < 7) {
            std::cout << "*** Attempt to read an eight byte value past end of webm block" << std::endl;
            exit(1);
        }
        if (remove_marker) {
            id_byte_0 &= ~0xFF;
        }
        uint64_t value = id_byte_0;
        for (size_t i = 0; i < 7; i += 1) {
            value = (value << 8) | mData[mPos++];
        }
        return value;
    } else {
        std::cout << "*** Invalid ID encoding found" << std::endl;
        exit(1);
    }
}

/////

constexpr uint32_t kID_Header = 0x1A45DFA3;
constexpr uint32_t kID_EMBLVersion = 0x4286;
constexpr uint32_t kID_DocType = 0x4282;
constexpr uint32_t kID_Segment = 0x18538067;
constexpr uint32_t kID_Tracks = 0x1654AE6B;
constexpr uint32_t kID_SegmentInformation = 0x1549A966;
constexpr uint32_t kID_TimecodeScale = 0x2AD7B1;
constexpr uint32_t kID_Cluster = 0x1F43B675;
constexpr uint32_t kID_TrackEntry = 0xAE;
constexpr uint32_t kID_TrackNumber = 0xD7;
constexpr uint32_t kID_TrackType = 0x83;
constexpr uint32_t kID_CodecID = 0x86;
constexpr uint32_t kID_Timecode = 0xE7;
constexpr uint32_t kID_SimpleBlock = 0xA3;

class WebmLoader
{
public:
    WebmLoader(const srtc::ByteBuffer& data, LoadedMedia& loaded_media);
    ~WebmLoader();

    void process();
    void printInfo();

private:
    const srtc::ByteBuffer& mData;
    LoadedMedia& mLoadedMedia;

    uint32_t mTimecodeScaleNS;

    uint32_t mTrackNumberVP8;

    uint32_t mAllFrameCountVP8;
    uint32_t mKeyFrameCountVP8;

    int64_t mCurrPTS;

    void parseSegmentInformationElement(const uint8_t* data, uint64_t size);
    void parseTracksElement(const uint8_t* data, uint64_t size);
    void parseClusterElement(const uint8_t* data, uint64_t size);
    void parseSimpleBlock(const uint8_t* data, uint64_t size, uint32_t cluster_timecode);
};

WebmLoader::WebmLoader(const srtc::ByteBuffer& data, LoadedMedia& loaded_media)
    : mData(data)
    , mLoadedMedia(loaded_media)
    , mTrackNumberVP8(0)
    , mTimecodeScaleNS(1000000)
    , mAllFrameCountVP8(0)
    , mKeyFrameCountVP8(0)
    , mCurrPTS(0)
{
}

WebmLoader::~WebmLoader() = default;

void WebmLoader::process()
{
    // Parse overall structure to validate the header and find the segment

    const uint8_t* segment_data = nullptr;
    uint64_t segment_size = 0;

    {
        WebmReader file_reader(mData.data(), mData.size());

        uint32_t header_id;
        uint64_t header_size;
        file_reader.readBlockHeader(header_id, header_size);

        if (header_id != kID_Header) {
            std::cout << "Invalid webm file header" << std::endl;
            exit(1);
        }

        // Parse into the header
        bool header_vers_present = false;
        bool header_webm_present = false;

        WebmReader header_reader(file_reader.curr(), header_size);
        while (header_reader.remaining() > 0) {
            uint32_t header_item_id;
            uint64_t header_item_size;
            header_reader.readBlockHeader(header_item_id, header_item_size);

            switch (header_item_id) {
            case kID_EMBLVersion:
                if (header_item_size != 1 || header_reader.remaining() < 1 || header_reader.curr()[0] != 0x01) {
                    std::cout << "Invalid webm file header" << std::endl;
                    exit(1);
                }
                header_vers_present = true;
                break;
            case kID_DocType:
                if (header_item_size != 4 || header_reader.remaining() < 4 ||
                    std::memcmp(header_reader.curr(), "webm", 4) != 0) {
                    std::cout << "Invalid webm file header" << std::endl;
                    exit(1);
                }
                header_webm_present = true;
                break;
            default:
                break;
            }

            header_reader.skip(header_item_size);
        }

        if (!header_vers_present || !header_webm_present) {
            std::cout << "Invalid webm file header" << std::endl;
            exit(1);
        }

        // Continue parsing the overall file
        file_reader.skip(header_size);

        while (file_reader.remaining() > 0) {
            uint32_t file_item_id;
            uint64_t file_item_size;
            file_reader.readBlockHeader(file_item_id, file_item_size);

            if (file_item_id == kID_Segment) {
                segment_data = file_reader.curr();
                segment_size = file_item_size;
                break;
            } else {
                file_reader.skip(file_item_size);
            }
        }

        if (segment_data == nullptr || segment_size == 0) {
            std::cout << "Segment entry not found in the webm file" << std::endl;
            exit(1);
        }
    }

    // Parse the segment

    {
        WebmReader segment_reader(segment_data, segment_size);

        while (segment_reader.remaining() > 0) {
            uint32_t segment_item_id;
            uint64_t segment_item_size;
            segment_reader.readBlockHeader(segment_item_id, segment_item_size);

            if (segment_item_id == kID_SegmentInformation) {
                parseSegmentInformationElement(segment_reader.curr(), segment_item_size);
            } else if (segment_item_id == kID_Tracks) {
                parseTracksElement(segment_reader.curr(), segment_item_size);
            } else if (segment_item_id == kID_Cluster) {
                parseClusterElement(segment_reader.curr(), segment_item_size);
            }

            segment_reader.skip(segment_item_size);
        }
    }
}

void WebmLoader::printInfo()
{
    std::cout << "*** Frame count:     " << std::setw(4) << mAllFrameCountVP8 << std::endl;
    std::cout << "*** Key frame count: " << std::setw(4) << mKeyFrameCountVP8 << std::endl;
}

void WebmLoader::parseSegmentInformationElement(const uint8_t* data, uint64_t size)
{
    WebmReader info_reader(data, size);
    while (info_reader.remaining() > 0) {
        uint32_t info_item_id;
        uint64_t info_item_size;
        info_reader.readBlockHeader(info_item_id, info_item_size);

        if (info_item_id == kID_TimecodeScale) {
            mTimecodeScaleNS = info_reader.readUInt(info_item_size);
        } else {
            info_reader.skip(info_item_size);
        }
    }
}

void WebmLoader::parseTracksElement(const uint8_t* data, uint64_t size)
{
    WebmReader tracks_reader(data, size);
    while (tracks_reader.remaining() > 0) {
        uint32_t tracks_item_id;
        uint64_t tracks_item_size;
        tracks_reader.readBlockHeader(tracks_item_id, tracks_item_size);

        if (tracks_item_id == kID_TrackEntry) {
            std::optional<uint32_t> track_number;
            std::optional<uint32_t> track_type;
            std::string track_codec_id;

            WebmReader track_entry_reader(tracks_reader.curr(), tracks_item_size);
            while (track_entry_reader.remaining() > 0) {
                uint32_t track_entry_item_id;
                uint64_t track_entry_item_size;
                track_entry_reader.readBlockHeader(track_entry_item_id, track_entry_item_size);

                switch (track_entry_item_id) {
                case kID_TrackNumber:
                    track_number = track_entry_reader.readUInt(track_entry_item_size);
                    break;
                case kID_TrackType:
                    track_type = track_entry_reader.readUInt(track_entry_item_size);
                    break;
                case kID_CodecID:
                    track_codec_id = track_entry_reader.readString(track_entry_item_size);
                    break;
                default:
                    track_entry_reader.skip(track_entry_item_size);
                    break;
                }
            }

            if (track_number.has_value() && track_type.has_value() && track_type.value() == 1 &&
                track_codec_id == "V_VP8") {
                mTrackNumberVP8 = track_number.value();
            }
        }
        tracks_reader.skip(tracks_item_size);
    }

    if (mTrackNumberVP8 == 0) {
        std::cout << "Cannot find a VP8 track in this the webm file" << std::endl;
        exit(1);
    }
}

void WebmLoader::parseClusterElement(const uint8_t* data, uint64_t size)
{
    uint32_t timecode = 0;

    WebmReader cluster_reader(data, size);
    while (cluster_reader.remaining() > 0) {
        uint32_t cluster_item_id;
        uint64_t cluster_item_size;
        cluster_reader.readBlockHeader(cluster_item_id, cluster_item_size);

        if (cluster_item_id == kID_Timecode) {
            timecode = cluster_reader.readUInt(cluster_item_size);
        } else if (cluster_item_id == kID_SimpleBlock) {
            parseSimpleBlock(cluster_reader.curr(), cluster_item_size, timecode);
            cluster_reader.skip(cluster_item_size);
        } else {
            cluster_reader.skip(cluster_item_size);
        }
    }
}

void WebmLoader::parseSimpleBlock(const uint8_t* data, uint64_t size, uint32_t cluster_timecode)
{
    WebmReader block_reader(data, size);

    const auto track_number = block_reader.readVInt32();
    if (track_number != mTrackNumberVP8) {
        return;
    }

    mAllFrameCountVP8 += 1;

    LoadedFrame loaded_frame = {};
    loaded_frame.pts_usec = mCurrPTS;
    loaded_frame.frame = { data, size };

    mLoadedMedia.frame_list.push_back(std::move(loaded_frame));

    mCurrPTS += 40 * 1000; // for now assume 25 fps, later we'll use the actual frame timestamps from the file

    const auto frame_offset = block_reader.readFixedInt16();
    const auto frame_flags = block_reader.readFixedUInt8();

    if ((frame_flags & 0x80) == 0x80) {
        // A key frame
        mKeyFrameCountVP8 += 1;


        const auto dump = srtc::bin_to_hex(block_reader.curr(), std::min<size_t>(block_reader.remaining(), 16));
        std::printf("Key frame %2u, size = %zu: %s\n", mKeyFrameCountVP8, block_reader.remaining(), dump.c_str());

        // Decode key frame data
        const auto frame_data = block_reader.curr();
        const auto frame_size = block_reader.remaining();

        const auto tag = frame_data[0] | (frame_data[1] << 8) | (frame_data[2] << 16);
        const auto tag_frame_type = tag & 0x01;
        const auto tag_version = (tag >> 1) & 0x07;
        const auto tag_show_frame = (tag >> 4) & 0x01;
        const auto tag_first_partition_size = tag >> 5;

        std::printf("  Partition size = %u\n", tag_first_partition_size);

        const auto frame_width_data = frame_data + 6;
        const auto frame_width = ((frame_width_data[1] << 8) | frame_width_data[0]) & 0x3FFF;
        const auto frame_height_data = frame_data + 8;
        const auto frame_height = ((frame_height_data[1] << 8) | frame_height_data[0]) & 0x3FFF;

        char fname[128];
        std::snprintf(fname, sizeof(fname), "key-frame-%u.ivf", mKeyFrameCountVP8);

        const auto file = std::fopen(fname, "wb");
        if (file) {
            // Write IVF header (32 bytes)
            std::fwrite("DKIF", 4, 1, file); // signature

            uint16_t version = 0;
            uint16_t header_len = 32;
            std::fwrite(&version, 2, 1, file);
            std::fwrite(&header_len, 2, 1, file);

            std::fwrite("VP80", 4, 1, file); // codec fourcc

            uint16_t width = frame_width;
            uint16_t height = frame_height;
            std::fwrite(&width, 2, 1, file);
            std::fwrite(&height, 2, 1, file);

            uint32_t frame_rate_num = 30;
            uint32_t frame_rate_den = 1;
            uint32_t frame_count = 1;
            uint32_t unused = 0;
            std::fwrite(&frame_rate_num, 4, 1, file);
            std::fwrite(&frame_rate_den, 4, 1, file);
            std::fwrite(&frame_count, 4, 1, file);
            std::fwrite(&unused, 4, 1, file);

            // Write frame header (12 bytes)
            const auto frame_size_u32 = static_cast<uint32_t>(frame_size);
            uint64_t timestamp = 0;
            std::fwrite(&frame_size_u32, 4, 1, file);
            std::fwrite(&timestamp, 8, 1, file);

            // Write VP8 frame data
            std::fwrite(frame_data, frame_size, 1, file);
            std::fclose(file);
        }
    }
}

/////

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

    WebmLoader loader(data, loaded_media);

    loader.process();

    if (print_info) {
        loader.printInfo();
    }

    return loaded_media;
}
