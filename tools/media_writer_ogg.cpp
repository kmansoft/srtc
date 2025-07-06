#include "media_writer_ogg.h"

MediaWriterOgg::MediaWriterOgg(const std::string& filename, const std::shared_ptr<srtc::Track>& track)
	: MediaWriter(filename)
	, mTrack(track)
	, mFile(nullptr)
	, m_packetno(0)
	, m_granulepos(0)
	, m_os()
	, m_og()
	, m_op()
	, m_outPacketCount(0)
	, m_outPageCount(0)
	, m_outByteCount(0)
{
}

MediaWriterOgg::~MediaWriterOgg()
{
	if (mFile) {
		while (ogg_stream_flush(&m_os, &m_og)) {
			fwrite(m_og.header, 1, m_og.header_len, mFile);
			fwrite(m_og.body, 1, m_og.body_len, mFile);
		}

		ogg_stream_clear(&m_os);

		fclose(mFile);
		mFile = nullptr;

		std::printf("OGG: Wrote %zu packets, %zu pages, %zu bytes to %s\n",
					m_outPacketCount,
					m_outPageCount,
					m_outByteCount,
					mFilename.c_str());
	}
}

void MediaWriterOgg::write(const std::shared_ptr<srtc::EncodedFrame>& frame)
{
	const auto sample_rate = mTrack->getClockRate();
	const auto options = mTrack->getCodecOptions();

	if (!mFile) {
		// Open the file
		mFile = fopen(mFilename.c_str(), "wb");
		if (!mFile) {
			perror("OGG: Cannot open output file");
			return;
		}

		std::printf("OGG: Opened output file %s\n", mFilename.c_str());

		// Write the header
		const auto channels = static_cast<uint8_t>(options->stereo ? 2 : 1);

		ogg_stream_init(&m_os, rand());

		unsigned char id_header[19] = {
			'O',
			'p',
			'u',
			's',
			'H',
			'e',
			'a',
			'd',	  // "OpusHead"
			1,		  // version
			channels, // channels
			0,
			0, // pre-skip (little endian)
			static_cast<uint8_t>(sample_rate & 0xff),
			static_cast<uint8_t>((sample_rate >> 8) & 0xff),
			static_cast<uint8_t>((sample_rate >> 16) & 0xff),
			static_cast<uint8_t>((sample_rate >> 24) & 0xff), // sample rate
			0,
			0, // output gain
			0  // channel mapping family
		};

		m_op.packet = id_header;
		m_op.bytes = 19;
		m_op.b_o_s = 1; // beginning of stream
		m_op.e_o_s = 0;
		m_op.granulepos = 0;
		m_op.packetno = 0;

		ogg_stream_packetin(&m_os, &m_op);

		// Write header pages
		while (ogg_stream_pageout(&m_os, &m_og)) {
			fwrite(m_og.header, 1, m_og.header_len, mFile);
			fwrite(m_og.body, 1, m_og.body_len, mFile);
		}

		m_packetno = 1;
	}

	// Write the packet
	m_op.packet = frame->data.data();
	m_op.bytes = frame->data.size();
	m_op.b_o_s = 0;
	m_op.e_o_s = 0;
	m_op.granulepos = m_granulepos;
	m_op.packetno = m_packetno;

	ogg_stream_packetin(&m_os, &m_op);

	m_outPacketCount += 1;

	while (ogg_stream_pageout(&m_os, &m_og)) {
		fwrite(m_og.header, 1, m_og.header_len, mFile);
		fwrite(m_og.body, 1, m_og.body_len, mFile);

		m_outPageCount += 1;
		m_outByteCount += m_og.header_len;
		m_outByteCount += m_og.body_len;
	}

	m_granulepos += options->minptime * mTrack->getClockRate() / 1000;
	m_packetno += 1;
}
