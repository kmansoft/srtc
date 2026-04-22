#pragma once

#include <cstddef>
#include <cstdint>

namespace srtc::vp9
{

// VP9 RTP payload descriptor (RFC 9628 §4)
struct PayloadDescriptor {
    bool picture_id_present; // I bit
    bool inter_picture;      // P bit
    bool start_of_frame;     // B bit
    bool end_of_frame;       // E bit
    uint16_t picture_id;     // 7- or 15-bit picture ID (if I=1)
};

// Parse the VP9 payload descriptor from the start of an RTP payload.
// Returns true on success; outPayloadData/Size point past the descriptor at the raw VP9 bitstream.
bool parsePayloadDescriptor(const uint8_t* data,
                            size_t size,
                            PayloadDescriptor& desc,
                            const uint8_t*& outPayloadData,
                            size_t& outPayloadSize);

// Write a VP9 payload descriptor (I=1, M=1, 15-bit picture ID) into buf.
// Returns the number of bytes written (always 3).
size_t buildPayloadDescriptor(uint8_t* buf,
                              size_t bufSize,
                              bool startOfFrame,
                              bool endOfFrame,
                              bool interPicture,
                              uint16_t pictureId);

// Returns true if the raw VP9 bitstream starts a key frame.
bool isKeyFrame(const uint8_t* data, size_t size);

// Extracts frame dimensions from a VP9 key frame bitstream.
// Returns true on success.
bool extractDimensions(const uint8_t* data, size_t size, uint16_t& width, uint16_t& height);

} // namespace srtc::vp9
