#include "media_writer_vp8.h"

#include <cstdlib>
#include <cstring>

namespace
{

// Cross-platform byte swapping
inline uint16_t bswap16(uint16_t x)
{
#ifdef _MSC_VER
    return _byteswap_ushort(x);
#else
    return __builtin_bswap16(x);
#endif
}

inline uint32_t bswap32(uint32_t x)
{
#ifdef _MSC_VER
    return _byteswap_ulong(x);
#else
    return __builtin_bswap32(x);
#endif
}

inline uint64_t bswap64(uint64_t x)
{
#ifdef _MSC_VER
    return _byteswap_uint64(x);
#else
    return __builtin_bswap64(x);
#endif
}

} // namespace

MediaWriterVP8::MediaWriterVP8(const std::string& filename, const std::shared_ptr<srtc::Track>& track)
    : MediaWriter(filename)
    , mTrack(track)
    , mOutAllFrameCount(0)
    , mOutKeyFrameCount(0)
    , mOutByteCount(0)
    , mBaseRtpTimestamp(0)
{
    checkExtension({ ".webm" });
}

MediaWriterVP8::~MediaWriterVP8()
{
    if (mOutAllFrameCount > 0 && mOutByteCount > 0) {
        writeWebM();

        std::printf("VP8: Wrote %zu frames, %zu key frames, %zu bytes to %s\n",
                    mOutAllFrameCount,
                    mOutKeyFrameCount,
                    mOutByteCount,
                    mFilename.c_str());
    }
}

void MediaWriterVP8::write(const std::shared_ptr<srtc::EncodedFrame>& frame)
{
    // Check if it's a key frame
    const auto frameData = frame->data.data();
    const auto frameSize = frame->data.size();

    if (frameSize < 3) {
        return;
    }

    const auto tag = frameData[0] | (frameData[1] << 8) | (frameData[2] << 16);
    const auto tagFrameType = tag & 0x01;

    if (tagFrameType == 0 && frameSize > 10) {
        // Maintain key frame count
        mOutKeyFrameCount += 1;
    }

    // Calculate pts
    int64_t pts_usec = 0;
    if (mOutAllFrameCount == 0) {
        mBaseRtpTimestamp = frame->rtp_timestamp_ext;
        std::printf("VP8: Started buffering video frames, will save when exiting from Ctrl+C\n");
    } else {
        pts_usec = static_cast<int64_t>(frame->rtp_timestamp_ext - mBaseRtpTimestamp) * 1000 / 90;
    }

    mOutAllFrameCount += 1;
    mOutByteCount += frame->data.size();

    VP8Frame outFrame;
    outFrame.pts_usec = pts_usec;
    outFrame.data = std::move(frame->data);

    mFrameList.push_back(std::move(outFrame));
}

void MediaWriterVP8::writeWebM()
{
    if (mFrameList.empty()) {
        return;
    }

    FILE* file = std::fopen(mFilename.c_str(), "wb");
    if (!file) {
        std::printf("Failed to create %s\n", mFilename.c_str());
        return;
    }

    // Calculate duration
    uint64_t duration_ns = 0;
    if (mFrameList.size() > 1) {
        duration_ns = static_cast<uint64_t>(mFrameList.back().pts_usec - mFrameList.front().pts_usec) * 1000;
    }

    writeEBMLHeader(file);

    // Write Segment header with proper size calculation
    // First write to temporary buffer to calculate size
    std::vector<uint8_t> segment_content;

    // Create temporary file for size calculation
    FILE* temp_file = tmpfile();
    writeSegmentInfo(temp_file, duration_ns);
    writeTracks(temp_file);
    writeClusters(temp_file);

    // Get size and copy data
    long segment_data_size = ftell(temp_file);
    segment_content.resize(segment_data_size);
    fseek(temp_file, 0, SEEK_SET);
    fread(segment_content.data(), segment_data_size, 1, temp_file);
    fclose(temp_file);

    // Write Segment with known size
    uint32_t segment_id = 0x18538067;
    writeEBMLElement(file, segment_id, segment_content.data(), segment_data_size);

    std::fclose(file);
}

void MediaWriterVP8::writeEBMLHeader(FILE* file)
{
    // EBML Header (0x1A45DFA3)
    const uint32_t ebml_header_id = 0x1A45DFA3;

    // Build header content
    std::vector<uint8_t> header_data;

    // EBMLVersion (0x4286) = 1
    uint8_t ebml_version[] = { 0x42, 0x86, 0x81, 0x01 };
    header_data.insert(header_data.end(), ebml_version, ebml_version + sizeof(ebml_version));

    // EBMLReadVersion (0x42F7) = 1
    uint8_t ebml_read_version[] = { 0x42, 0xF7, 0x81, 0x01 };
    header_data.insert(header_data.end(), ebml_read_version, ebml_read_version + sizeof(ebml_read_version));

    // EBMLMaxIDLength (0x42F2) = 4
    uint8_t ebml_max_id[] = { 0x42, 0xF2, 0x81, 0x04 };
    header_data.insert(header_data.end(), ebml_max_id, ebml_max_id + sizeof(ebml_max_id));

    // EBMLMaxSizeLength (0x42F3) = 8
    uint8_t ebml_max_size[] = { 0x42, 0xF3, 0x81, 0x08 };
    header_data.insert(header_data.end(), ebml_max_size, ebml_max_size + sizeof(ebml_max_size));

    // DocType (0x4282) = "webm"
    const char* doctype = "webm";
    uint8_t doctype_header[] = { 0x42, 0x82, 0x84 };
    header_data.insert(header_data.end(), doctype_header, doctype_header + sizeof(doctype_header));
    header_data.insert(header_data.end(), doctype, doctype + strlen(doctype));

    // DocTypeVersion (0x4287) = 2
    uint8_t doctype_version[] = { 0x42, 0x87, 0x81, 0x02 };
    header_data.insert(header_data.end(), doctype_version, doctype_version + sizeof(doctype_version));

    // DocTypeReadVersion (0x4285) = 2
    uint8_t doctype_read_version[] = { 0x42, 0x85, 0x81, 0x02 };
    header_data.insert(header_data.end(), doctype_read_version, doctype_read_version + sizeof(doctype_read_version));

    writeEBMLElement(file, ebml_header_id, header_data.data(), header_data.size());
}

void MediaWriterVP8::writeSegmentInfo(FILE* file, uint64_t duration_ns)
{
    // Info (0x1549A966)
    const uint32_t info_id = 0x1549A966;

    std::vector<uint8_t> info_data;

    // TimecodeScale (0x2AD7B1) = 1000000 (1ms)
    uint32_t timecode_scale = 1000000;
    uint8_t timecode_header[] = { 0x2A, 0xD7, 0xB1, 0x83 };
    info_data.insert(info_data.end(), timecode_header, timecode_header + sizeof(timecode_header));

    timecode_scale = bswap32(timecode_scale);
    info_data.insert(info_data.end(),
                     reinterpret_cast<uint8_t*>(&timecode_scale) + 1,
                     reinterpret_cast<uint8_t*>(&timecode_scale) + 4);

    // MuxingApp (0x4D80) = "srtc"
    const char* muxing_app = "srtc";
    uint8_t muxing_header[] = { 0x4D, 0x80, 0x84 };
    info_data.insert(info_data.end(), muxing_header, muxing_header + sizeof(muxing_header));
    info_data.insert(info_data.end(), muxing_app, muxing_app + strlen(muxing_app));

    // WritingApp (0x5741) = "srtc"
    const char* writing_app = "srtc";
    uint8_t writing_header[] = { 0x57, 0x41, 0x84 };
    info_data.insert(info_data.end(), writing_header, writing_header + sizeof(writing_header));
    info_data.insert(info_data.end(), writing_app, writing_app + strlen(writing_app));

    // Duration (0x4489) - optional
    if (duration_ns > 0) {
        double duration_ms = static_cast<double>(duration_ns) / 1000000.0;
        uint8_t duration_header[] = { 0x44, 0x89, 0x88 };
        info_data.insert(info_data.end(), duration_header, duration_header + sizeof(duration_header));

        uint64_t duration_bits;
        memcpy(&duration_bits, &duration_ms, 8);
        duration_bits = bswap64(duration_bits);
        info_data.insert(info_data.end(),
                         reinterpret_cast<uint8_t*>(&duration_bits),
                         reinterpret_cast<uint8_t*>(&duration_bits) + 8);
    }

    writeEBMLElement(file, info_id, info_data.data(), info_data.size());
}

void MediaWriterVP8::writeTracks(FILE* file)
{
    // Tracks (0x1654AE6B)
    const uint32_t tracks_id = 0x1654AE6B;

    std::vector<uint8_t> tracks_data;

    // TrackEntry (0xAE)
    std::vector<uint8_t> track_data;

    // TrackNumber (0xD7) = 1
    uint8_t track_number[] = { 0xD7, 0x81, 0x01 };
    track_data.insert(track_data.end(), track_number, track_number + sizeof(track_number));

    // TrackUID (0x73C5) = 1
    uint8_t track_uid[] = { 0x73, 0xC5, 0x81, 0x01 };
    track_data.insert(track_data.end(), track_uid, track_uid + sizeof(track_uid));

    // TrackType (0x83) = 1 (video)
    uint8_t track_type[] = { 0x83, 0x81, 0x01 };
    track_data.insert(track_data.end(), track_type, track_type + sizeof(track_type));

    // CodecID (0x86) = "V_VP8"
    const char* codec_id = "V_VP8";
    uint8_t codec_header[] = { 0x86, 0x85 };
    track_data.insert(track_data.end(), codec_header, codec_header + sizeof(codec_header));
    track_data.insert(track_data.end(), codec_id, codec_id + strlen(codec_id));

    // Video (0xE0)
    std::vector<uint8_t> video_data;

    // Extract actual dimensions from VP8 frames
    uint16_t frame_width = 1920;  // fallback
    uint16_t frame_height = 1080; // fallback
    extractVP8Dimensions(frame_width, frame_height);

    // PixelWidth (0xB0)
    uint16_t pixel_width = bswap16(frame_width);
    uint8_t width_header[] = { 0xB0, 0x82 };
    video_data.insert(video_data.end(), width_header, width_header + sizeof(width_header));
    video_data.insert(
        video_data.end(), reinterpret_cast<uint8_t*>(&pixel_width), reinterpret_cast<uint8_t*>(&pixel_width) + 2);

    // PixelHeight (0xBA)
    uint16_t pixel_height = bswap16(frame_height);
    uint8_t height_header[] = { 0xBA, 0x82 };
    video_data.insert(video_data.end(), height_header, height_header + sizeof(height_header));
    video_data.insert(
        video_data.end(), reinterpret_cast<uint8_t*>(&pixel_height), reinterpret_cast<uint8_t*>(&pixel_height) + 2);

    // Write Video element
    uint8_t video_header[] = { 0xE0 };
    track_data.insert(track_data.end(), video_header, video_header + sizeof(video_header));

    std::vector<uint8_t> video_size_bytes;
    uint64_t video_size = video_data.size();
    int width = getVarIntWidth(video_size);
    uint8_t first_byte = (1 << (8 - width)) | ((video_size >> ((width - 1) * 8)) & ((1 << (8 - width)) - 1));
    video_size_bytes.push_back(first_byte);
    for (int i = width - 2; i >= 0; i--) {
        video_size_bytes.push_back((video_size >> (i * 8)) & 0xFF);
    }
    track_data.insert(track_data.end(), video_size_bytes.begin(), video_size_bytes.end());
    track_data.insert(track_data.end(), video_data.begin(), video_data.end());

    // Write TrackEntry
    uint8_t track_entry_header[] = { 0xAE };
    tracks_data.insert(tracks_data.end(), track_entry_header, track_entry_header + sizeof(track_entry_header));

    std::vector<uint8_t> track_size_bytes;
    uint64_t track_size = track_data.size();
    int width2 = getVarIntWidth(track_size);
    uint8_t track_first_byte = (1 << (8 - width2)) | ((track_size >> ((width2 - 1) * 8)) & ((1 << (8 - width2)) - 1));
    track_size_bytes.push_back(track_first_byte);
    for (int i = width2 - 2; i >= 0; i--) {
        track_size_bytes.push_back((track_size >> (i * 8)) & 0xFF);
    }
    tracks_data.insert(tracks_data.end(), track_size_bytes.begin(), track_size_bytes.end());
    tracks_data.insert(tracks_data.end(), track_data.begin(), track_data.end());

    writeEBMLElement(file, tracks_id, tracks_data.data(), tracks_data.size());
}

void MediaWriterVP8::writeClusters(FILE* file)
{
    if (mFrameList.empty())
        return;

    // Write one cluster per frame for simplicity
    for (size_t i = 0; i < mFrameList.size(); i++) {
        const auto& frame = mFrameList[i];

        // Cluster (0x1F43B675)
        const uint32_t cluster_id = 0x1F43B675;

        std::vector<uint8_t> cluster_data;

        // Timecode (0xE7) - in milliseconds
        uint64_t timecode_ms = frame.pts_usec / 1000;

        // Create temporary buffer for timecode element
        std::vector<uint8_t> timecode_data;
        int tc_width = getVarIntWidth(timecode_ms);
        for (int j = tc_width - 1; j >= 0; j--) {
            timecode_data.push_back((timecode_ms >> (j * 8)) & 0xFF);
        }

        // Write timecode element header manually
        cluster_data.push_back(0xE7); // Timecode ID
        writeVarIntToBuffer(cluster_data, timecode_data.size());
        cluster_data.insert(cluster_data.end(), timecode_data.begin(), timecode_data.end());

        // SimpleBlock (0xA3)
        std::vector<uint8_t> block_data;

        // Track number (1) - variable integer
        block_data.push_back(0x81); // track 1

        // Timestamp relative to cluster (0 for now)
        block_data.push_back(0x00);
        block_data.push_back(0x00);

        // Flags - keyframe detection
        uint8_t flags = 0x00;
        if (i == 0 || isKeyFrame(frame)) {
            flags |= 0x80; // Keyframe
        }
        block_data.push_back(flags);

        // Frame data
        block_data.insert(block_data.end(), frame.data.data(), frame.data.data() + frame.data.size());

        // Write SimpleBlock
        uint8_t block_header[] = { 0xA3 };
        cluster_data.insert(cluster_data.end(), block_header, block_header + sizeof(block_header));

        std::vector<uint8_t> block_size_bytes;
        uint64_t block_size = block_data.size();
        int width = getVarIntWidth(block_size);
        uint8_t first_byte = (1 << (8 - width)) | ((block_size >> ((width - 1) * 8)) & ((1 << (8 - width)) - 1));
        block_size_bytes.push_back(first_byte);
        for (int j = width - 2; j >= 0; j--) {
            block_size_bytes.push_back((block_size >> (j * 8)) & 0xFF);
        }
        cluster_data.insert(cluster_data.end(), block_size_bytes.begin(), block_size_bytes.end());
        cluster_data.insert(cluster_data.end(), block_data.begin(), block_data.end());

        writeEBMLElement(file, cluster_id, cluster_data.data(), cluster_data.size());
    }
}

bool MediaWriterVP8::isKeyFrame(const VP8Frame& frame)
{
    if (frame.data.size() < 3) {
        return false;
    }

    const auto* frameData = frame.data.data();
    const auto tag = frameData[0] | (frameData[1] << 8) | (frameData[2] << 16);
    const auto tagFrameType = tag & 0x01;

    return tagFrameType == 0;
}

void MediaWriterVP8::writeEBMLElement(FILE* file, uint32_t id, const void* data, size_t size)
{
    // Write element ID (big endian, variable length)
    if (id & 0xFF000000) {
        uint32_t id_be = bswap32(id);
        std::fwrite(&id_be, 4, 1, file);
    } else if (id & 0x00FF0000) {
        uint32_t id_be = bswap32(id << 8);
        std::fwrite(&id_be, 3, 1, file);
    } else if (id & 0x0000FF00) {
        uint16_t id_be = bswap16(static_cast<uint16_t>(id));
        std::fwrite(&id_be, 2, 1, file);
    } else {
        const auto id_byte = static_cast<uint8_t>(id);
        std::fwrite(&id_byte, 1, 1, file);
    }

    // Write size
    writeVarInt(file, size);

    // Write data
    if (data && size > 0) {
        std::fwrite(data, size, 1, file);
    }
}

void MediaWriterVP8::writeVarInt(FILE* file, uint64_t value)
{
    const auto width = getVarIntWidth(value);

    // EBML variable integer: first byte has leading 1 bit, followed by width-1 zero bits, then data
    uint8_t first_byte = (1 << (8 - width)) | ((value >> ((width - 1) * 8)) & ((1 << (8 - width)) - 1));
    std::fwrite(&first_byte, 1, 1, file);

    // Write remaining bytes
    for (int i = width - 2; i >= 0; i--) {
        uint8_t byte = (value >> (i * 8)) & 0xFF;
        std::fwrite(&byte, 1, 1, file);
    }
}

void MediaWriterVP8::writeVarIntToBuffer(std::vector<uint8_t>& buffer, uint64_t value)
{
    const auto width = getVarIntWidth(value);

    // EBML variable integer: first byte has leading 1 bit, followed by width-1 zero bits, then data
    uint8_t first_byte = (1 << (8 - width)) | ((value >> ((width - 1) * 8)) & ((1 << (8 - width)) - 1));
    buffer.push_back(first_byte);

    // Write remaining bytes
    for (int i = width - 2; i >= 0; i--) {
        uint8_t byte = (value >> (i * 8)) & 0xFF;
        buffer.push_back(byte);
    }
}

int MediaWriterVP8::getVarIntWidth(uint64_t value)
{
    if (value <= 127)
        return 1; // 2^7 - 1
    if (value <= 16383)
        return 2; // 2^14 - 1
    if (value <= 2097151)
        return 3; // 2^21 - 1
    if (value <= 268435455)
        return 4; // 2^28 - 1
    if (value <= 34359738367ULL)
        return 5; // 2^35 - 1
    if (value <= 4398046511103ULL)
        return 6; // 2^42 - 1
    if (value <= 562949953421311ULL)
        return 7; // 2^49 - 1
    return 8;
}

bool MediaWriterVP8::extractVP8Dimensions(uint16_t& width, uint16_t& height) const
{
    // Find first keyframe
    for (const auto& frame : mFrameList) {
        if (isKeyFrame(frame) && frame.data.size() >= 10) {
            const auto* frameData = frame.data.data();

            // VP8 keyframe structure (RFC 6386 Section 9.1)
            // Skip the 3-byte frame tag
            const auto* uncompressed_data_chunk = frameData + 3;

            // Skip start code (3 bytes: 0x9d 0x01 0x2a for keyframes)
            if (frame.data.size() >= 6 && uncompressed_data_chunk[0] == 0x9d && uncompressed_data_chunk[1] == 0x01 &&
                uncompressed_data_chunk[2] == 0x2a) {

                if (frame.data.size() >= 10) {
                    // Width and height are at bytes 6-7 and 8-9 respectively
                    const auto* width_data = frameData + 6;
                    const auto* height_data = frameData + 8;

                    // Extract 14-bit width and height (little-endian)
                    width = ((width_data[1] << 8) | width_data[0]) & 0x3FFF;
                    height = ((height_data[1] << 8) | height_data[0]) & 0x3FFF;

                    return true;
                }
            }
        }
    }

    // No keyframe found or unable to extract dimensions
    return false;
}
