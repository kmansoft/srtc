#include "media_writer_vp8.h"
#include "srtc/byte_buffer.h"

#include <cstdlib>
#include <cstring>

namespace
{

// Helper to write variable-length EBML integer to ByteBuffer
void writeVarIntToBuffer(srtc::ByteWriter& writer, uint64_t value)
{
    int width = 1;
    if (value > 127) width = 2;
    if (value > 16383) width = 3;
    if (value > 2097151) width = 4;
    if (value > 268435455) width = 5;
    if (value > 34359738367ULL) width = 6;
    if (value > 4398046511103ULL) width = 7;
    if (value > 562949953421311ULL) width = 8;

    uint8_t first_byte = (1 << (8 - width)) | ((value >> ((width - 1) * 8)) & ((1 << (8 - width)) - 1));
    writer.writeU8(first_byte);

    for (int i = width - 2; i >= 0; i--) {
        writer.writeU8((value >> (i * 8)) & 0xFF);
    }
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
    bool is_keyframe = (tagFrameType == 0 && frameSize > 10);

    if (is_keyframe) {
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
    outFrame.is_keyframe = is_keyframe;

    mFrameList.push_back(std::move(outFrame));
}

void MediaWriterVP8::writeWebM()
{
    if (mFrameList.empty()) {
        std::printf("VP8: Have not seen any video frames, won't write %s\n", mFilename.c_str());
        return;
    }

    FILE* file = std::fopen(mFilename.c_str(), "wb");
    if (!file) {
        std::printf("VP8: Failed to create %s\n", mFilename.c_str());
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
    if (fread(segment_content.data(), 1, segment_data_size, temp_file) != segment_data_size) {
        std::printf("VP8: Failed to read temporary file\n");
        exit(1);
    }
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

    // Build header content using ByteBuffer and ByteWriter
    srtc::ByteBuffer header_buffer;
    srtc::ByteWriter writer(header_buffer);

    // EBMLVersion (0x4286) = 1
    const uint8_t ebml_version[] = { 0x42, 0x86, 0x81, 0x01 };
    writer.write(ebml_version, sizeof(ebml_version));

    // EBMLReadVersion (0x42F7) = 1
    const uint8_t ebml_read_version[] = { 0x42, 0xF7, 0x81, 0x01 };
    writer.write(ebml_read_version, sizeof(ebml_read_version));

    // EBMLMaxIDLength (0x42F2) = 4
    const uint8_t ebml_max_id[] = { 0x42, 0xF2, 0x81, 0x04 };
    writer.write(ebml_max_id, sizeof(ebml_max_id));

    // EBMLMaxSizeLength (0x42F3) = 8
    const uint8_t ebml_max_size[] = { 0x42, 0xF3, 0x81, 0x08 };
    writer.write(ebml_max_size, sizeof(ebml_max_size));

    // DocType (0x4282) = "webm"
    const char* doctype = "webm";
    const uint8_t doctype_header[] = { 0x42, 0x82, 0x84 };
    writer.write(doctype_header, sizeof(doctype_header));
    writer.write(reinterpret_cast<const uint8_t*>(doctype), strlen(doctype));

    // DocTypeVersion (0x4287) = 2
    const uint8_t doctype_version[] = { 0x42, 0x87, 0x81, 0x02 };
    writer.write(doctype_version, sizeof(doctype_version));

    // DocTypeReadVersion (0x4285) = 2
    const uint8_t doctype_read_version[] = { 0x42, 0x85, 0x81, 0x02 };
    writer.write(doctype_read_version, sizeof(doctype_read_version));

    writeEBMLElement(file, ebml_header_id, header_buffer.data(), header_buffer.size());
}

void MediaWriterVP8::writeSegmentInfo(FILE* file, uint64_t duration_ns)
{
    // Info (0x1549A966)
    const uint32_t info_id = 0x1549A966;

    srtc::ByteBuffer info_buffer;
    srtc::ByteWriter writer(info_buffer);

    // TimecodeScale (0x2AD7B1) = 1000000 (1ms)
    uint32_t timecode_scale = 1000000;
    const uint8_t timecode_header[] = { 0x2A, 0xD7, 0xB1, 0x84 }; // 4-byte size
    writer.write(timecode_header, sizeof(timecode_header));
    writer.writeU32(timecode_scale);

    // MuxingApp (0x4D80) = "srtc"
    const char* muxing_app = "srtc";
    const uint8_t muxing_header[] = { 0x4D, 0x80, 0x84 };
    writer.write(muxing_header, sizeof(muxing_header));
    writer.write(reinterpret_cast<const uint8_t*>(muxing_app), strlen(muxing_app));

    // WritingApp (0x5741) = "srtc"
    const char* writing_app = "srtc";
    const uint8_t writing_header[] = { 0x57, 0x41, 0x84 };
    writer.write(writing_header, sizeof(writing_header));
    writer.write(reinterpret_cast<const uint8_t*>(writing_app), strlen(writing_app));

    // Duration (0x4489) - optional
    if (duration_ns > 0) {
        double duration_ms = static_cast<double>(duration_ns) / 1000000.0;
        const uint8_t duration_header[] = { 0x44, 0x89, 0x88 };
        writer.write(duration_header, sizeof(duration_header));

        uint64_t duration_bits;
        memcpy(&duration_bits, &duration_ms, 8);
        writer.writeU64(duration_bits);
    }

    writeEBMLElement(file, info_id, info_buffer.data(), info_buffer.size());
}

void MediaWriterVP8::writeTracks(FILE* file)
{
    // Tracks (0x1654AE6B)
    const uint32_t tracks_id = 0x1654AE6B;

    srtc::ByteBuffer tracks_buffer;
    srtc::ByteWriter tracks_writer(tracks_buffer);

    // TrackEntry (0xAE)
    srtc::ByteBuffer track_buffer;
    srtc::ByteWriter track_writer(track_buffer);

    // TrackNumber (0xD7) = 1
    const uint8_t track_number[] = { 0xD7, 0x81, 0x01 };
    track_writer.write(track_number, sizeof(track_number));

    // TrackUID (0x73C5) = 1
    const uint8_t track_uid[] = { 0x73, 0xC5, 0x81, 0x01 };
    track_writer.write(track_uid, sizeof(track_uid));

    // TrackType (0x83) = 1 (video)
    const uint8_t track_type[] = { 0x83, 0x81, 0x01 };
    track_writer.write(track_type, sizeof(track_type));

    // CodecID (0x86) = "V_VP8"
    const char* codec_id = "V_VP8";
    track_writer.writeU8(0x86);
    writeVarIntToBuffer(track_writer, strlen(codec_id));
    track_writer.write(reinterpret_cast<const uint8_t*>(codec_id), strlen(codec_id));

    // Video (0xE0)
    srtc::ByteBuffer video_buffer;
    srtc::ByteWriter video_writer(video_buffer);

    // Extract actual dimensions from VP8 frames
    uint16_t frame_width = 1920;  // fallback
    uint16_t frame_height = 1080; // fallback
    extractVP8Dimensions(frame_width, frame_height);

    // PixelWidth (0xB0)
    const uint8_t width_header[] = { 0xB0, 0x82 };
    video_writer.write(width_header, sizeof(width_header));
    video_writer.writeU16(frame_width);

    // PixelHeight (0xBA)
    const uint8_t height_header[] = { 0xBA, 0x82 };
    video_writer.write(height_header, sizeof(height_header));
    video_writer.writeU16(frame_height);

    // Write Video element
    const uint8_t video_header[] = { 0xE0 };
    track_writer.write(video_header, sizeof(video_header));
    writeVarIntToBuffer(track_writer, video_buffer.size());
    track_writer.write(video_buffer.data(), video_buffer.size());

    // Write TrackEntry
    const uint8_t track_entry_header[] = { 0xAE };
    tracks_writer.write(track_entry_header, sizeof(track_entry_header));
    writeVarIntToBuffer(tracks_writer, track_buffer.size());
    tracks_writer.write(track_buffer.data(), track_buffer.size());

    writeEBMLElement(file, tracks_id, tracks_buffer.data(), tracks_buffer.size());
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

        srtc::ByteBuffer cluster_buffer;
        srtc::ByteWriter cluster_writer(cluster_buffer);

        // Timecode (0xE7) - in milliseconds
        uint64_t timecode_ms = frame.pts_usec / 1000;

        // Create temporary buffer for timecode element
        srtc::ByteBuffer timecode_buffer;
        srtc::ByteWriter timecode_writer(timecode_buffer);
        int tc_width = getVarIntWidth(timecode_ms);
        for (int j = tc_width - 1; j >= 0; j--) {
            timecode_writer.writeU8((timecode_ms >> (j * 8)) & 0xFF);
        }

        // Write timecode element header manually
        cluster_writer.writeU8(0xE7); // Timecode ID
        writeVarIntToBuffer(cluster_writer, timecode_buffer.size());
        cluster_writer.write(timecode_buffer.data(), timecode_buffer.size());

        // SimpleBlock (0xA3)
        srtc::ByteBuffer block_buffer;
        srtc::ByteWriter block_writer(block_buffer);

        // Track number (1) - variable integer
        block_writer.writeU8(0x81); // track 1

        // Timestamp relative to cluster (0 for now)
        block_writer.writeU8(0x00);
        block_writer.writeU8(0x00);

        // Flags - keyframe detection
        uint8_t flags = 0x00;
        if (i == 0 || frame.is_keyframe) {
            flags |= 0x80; // Keyframe
        }
        block_writer.writeU8(flags);

        // Frame data
        block_writer.write(frame.data.data(), frame.data.size());

        // Write SimpleBlock
        const uint8_t block_header[] = { 0xA3 };
        cluster_writer.write(block_header, sizeof(block_header));
        writeVarIntToBuffer(cluster_writer, block_buffer.size());
        cluster_writer.write(block_buffer.data(), block_buffer.size());

        writeEBMLElement(file, cluster_id, cluster_buffer.data(), cluster_buffer.size());
    }
}


void MediaWriterVP8::writeEBMLElement(FILE* file, uint32_t id, const void* data, size_t size)
{
    // Write element ID (big endian, variable length)
    if (id & 0xFF000000) {
        uint8_t bytes[4] = { static_cast<uint8_t>((id >> 24) & 0xFF),
                             static_cast<uint8_t>((id >> 16) & 0xFF),
                             static_cast<uint8_t>((id >> 8) & 0xFF),
                             static_cast<uint8_t>(id & 0xFF) };
        std::fwrite(bytes, 4, 1, file);
    } else if (id & 0x00FF0000) {
        uint8_t bytes[3] = { static_cast<uint8_t>((id >> 16) & 0xFF),
                             static_cast<uint8_t>((id >> 8) & 0xFF),
                             static_cast<uint8_t>(id & 0xFF) };
        std::fwrite(bytes, 3, 1, file);
    } else if (id & 0x0000FF00) {
        uint8_t bytes[2] = { static_cast<uint8_t>((id >> 8) & 0xFF), static_cast<uint8_t>(id & 0xFF) };
        std::fwrite(bytes, 2, 1, file);
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
        if (frame.is_keyframe && frame.data.size() >= 10) {
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
