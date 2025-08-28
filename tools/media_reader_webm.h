#pragma once

#include "media_reader.h"

#include <cstdint>
#include <string>

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

//////////

class WebmLoader
{
public:
    WebmLoader(const srtc::ByteBuffer& data,
               const std::string& codecId,
               const std::string& codecName,
               LoadedMedia& loaded_media);
    ~WebmLoader();

    void process();
    void printInfo() const;

private:
    const srtc::ByteBuffer& mData;
    const std::string mCodecId;
    const std::string mCodecName;

    LoadedMedia& mLoadedMedia;

    uint32_t mTimecodeScaleNS;

    uint32_t mTrackNumberVideo;

    uint32_t mAllFrameCount;
    uint32_t mKeyFrameCount;

    void parseSegmentInformationElement(const uint8_t* data, uint64_t size);
    void parseTracksElement(const uint8_t* data, uint64_t size);
    void parseClusterElement(const uint8_t* data, uint64_t size);
    void parseSimpleBlock(const uint8_t* data, uint64_t size, uint32_t cluster_timecode);
};
