#include "srtc/codec_vp9.h"
#include "srtc/bit_reader.h"

namespace srtc::vp9
{

bool parsePayloadDescriptor(const uint8_t* data,
                            size_t size,
                            PayloadDescriptor& desc,
                            const uint8_t*& outPayloadData,
                            size_t& outPayloadSize)
{
    // https://www.rfc-editor.org/rfc/rfc9628#section-4

    if (size < 1) {
        return false;
    }

    size_t pos = 0;
    const uint8_t byte0 = data[pos++];

    const bool flagI = (byte0 & 0x80) != 0;
    const bool flagP = (byte0 & 0x40) != 0;
    const bool flagL = (byte0 & 0x20) != 0;
    const bool flagF = (byte0 & 0x10) != 0;
    const bool flagB = (byte0 & 0x08) != 0;
    const bool flagE = (byte0 & 0x04) != 0;
    const bool flagV = (byte0 & 0x02) != 0;

    desc.picture_id_present = flagI;
    desc.inter_picture = flagP;
    desc.start_of_frame = flagB;
    desc.end_of_frame = flagE;
    desc.picture_id = 0;

    // I: PictureID
    if (flagI) {
        if (pos >= size) {
            return false;
        }
        const uint8_t m_byte = data[pos++];
        if (m_byte & 0x80) {
            // M=1: 15-bit picture ID
            if (pos >= size) {
                return false;
            }
            desc.picture_id = static_cast<uint16_t>(((m_byte & 0x7F) << 8) | data[pos++]);
        } else {
            // M=0: 7-bit picture ID
            desc.picture_id = m_byte & 0x7F;
        }
    }

    // L: layer indices (TL0PICIDX + TID/U/SID/D byte)
    if (flagL) {
        if (pos + 2 > size) {
            return false;
        }
        pos += 2;
    }

    // F: flexible mode reference indices (only when P=1)
    if (flagF && flagP) {
        for (int i = 0; i < 3; i++) {
            if (pos >= size) {
                return false;
            }
            const uint8_t ref_byte = data[pos++];
            if (!(ref_byte & 0x01)) {
                break; // N=0: no more references
            }
        }
    }

    // V: scalability structure
    if (flagV) {
        if (pos >= size) {
            return false;
        }
        const uint8_t ss_byte = data[pos++];
        const uint8_t n_s = (ss_byte >> 5) & 0x07;
        const bool y_bit = (ss_byte & 0x10) != 0;
        const bool g_bit = (ss_byte & 0x08) != 0;

        if (y_bit) {
            // WIDTH and HEIGHT (2 bytes each) per spatial layer
            const size_t skip = static_cast<size_t>(n_s + 1) * 4;
            if (pos + skip > size) {
                return false;
            }
            pos += skip;
        }

        if (g_bit) {
            if (pos >= size) {
                return false;
            }
            const uint8_t n_g = data[pos++];
            for (uint8_t i = 0; i < n_g; i++) {
                if (pos >= size) {
                    return false;
                }
                const uint8_t pg_byte = data[pos++];
                const uint8_t r_count = (pg_byte >> 2) & 0x03;
                if (pos + r_count > size) {
                    return false;
                }
                pos += r_count;
            }
        }
    }

    outPayloadData = data + pos;
    outPayloadSize = size - pos;
    return true;
}

size_t buildPayloadDescriptor(uint8_t* buf,
                              size_t bufSize,
                              bool startOfFrame,
                              bool endOfFrame,
                              bool interPicture,
                              uint16_t pictureId)
{
    // https://www.rfc-editor.org/rfc/rfc9628#section-4
    // We always emit I=1 with M=1 (15-bit picture ID), so the descriptor is always 3 bytes.

    if (bufSize < 3) {
        return 0;
    }

    buf[0] = 0x80; // I=1
    if (interPicture) {
        buf[0] |= 0x40; // P=1
    }
    if (startOfFrame) {
        buf[0] |= 0x08; // B=1
    }
    if (endOfFrame) {
        buf[0] |= 0x04; // E=1
    }

    // M=1 (two-byte picture ID), pictureId is 15-bit
    buf[1] = static_cast<uint8_t>(0x80 | ((pictureId >> 8) & 0x7F));
    buf[2] = static_cast<uint8_t>(pictureId & 0xFF);

    return 3;
}

bool isKeyFrame(const uint8_t* data, size_t size)
{
    // https://www.webmproject.org/vp9/

    if (size < 1) {
        return false;
    }

    BitReader reader(data, size);

    // frame_marker [2 bits] must be 0b10
    if (reader.readBits(2) != 2) {
        return false;
    }

    // profile: low bit then high bit
    const auto profile_low = reader.readBit();
    const auto profile_high = reader.readBit();
    const auto profile = (profile_high << 1) | profile_low;

    // reserved_zero for profile 3
    if (profile == 3) {
        reader.readBit();
    }

    // show_existing_frame
    if (reader.readBit()) {
        return false;
    }

    // frame_type: 0 = KEY_FRAME, 1 = NON_KEY_FRAME
    return reader.readBit() == 0;
}

bool extractDimensions(const uint8_t* data, size_t size, uint16_t& width, uint16_t& height)
{
    if (size < 4) {
        return false;
    }

    BitReader reader(data, size);

    // frame_marker [2 bits] must be 0b10
    if (reader.readBits(2) != 2) {
        return false;
    }

    // profile
    const auto profile_low = reader.readBit();
    const auto profile_high = reader.readBit();
    const auto profile = (profile_high << 1) | profile_low;

    if (profile == 3) {
        reader.readBit(); // reserved_zero
    }

    // show_existing_frame: must be 0 for a key frame we're parsing
    if (reader.readBit()) {
        return false;
    }

    // frame_type: must be 0 (KEY_FRAME)
    if (reader.readBit() != 0) {
        return false;
    }

    reader.readBit(); // show_frame
    reader.readBit(); // error_resilient_mode

    // frame_sync_code: 0x49 0x83 0x42
    if (reader.readBits(8) != 0x49 || reader.readBits(8) != 0x83 || reader.readBits(8) != 0x42) {
        return false;
    }

    // color_config()
    const auto color_space = reader.readBits(3);
    if (color_space != 7) {
        reader.readBit(); // color_range
        if (profile == 1 || profile == 3) {
            reader.readBit(); // subsampling_x
            reader.readBit(); // subsampling_y
            reader.readBit(); // reserved_zero
        }
    } else {
        // SRGB implies full range; profile 1/3 has reserved_zero
        if (profile == 1 || profile == 3) {
            reader.readBit(); // reserved_zero
        }
    }

    // frame_size(): frame_width_minus_1 and frame_height_minus_1 are each 16 bits
    const auto w = reader.readBits(16);
    const auto h = reader.readBits(16);

    width = static_cast<uint16_t>(w + 1);
    height = static_cast<uint16_t>(h + 1);

    return true;
}

} // namespace srtc::vp9
