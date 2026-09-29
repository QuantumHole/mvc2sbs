#include "h264converter.h"

H264Converter::H264Converter(const std::string& input_file_name, const TrackInfo& info) :
	TrackConverter(input_file_name, info),
	m_nal_length_size(0),
	m_codec_private_written(false)
{
	if (info.codecPrivate.size() < 5)
	{
		return;
	}

	if (info.codecPrivate[0] != 1)
	{
		return;
	}

	/* lowest 2 bits contain lengthSize - 1. */
	/* AVC permits values of 1, 2 or 4 bytes. */
	m_nal_length_size = (info.codecPrivate[4] & 0x03) + 1;
}

H264Converter::~H264Converter(void)
{
}

const std::string H264Converter::output_extension(void) const
{
	return "." + m_track_info.language + ".264";
}

void H264Converter::write_start_code(void)
{
	static constexpr uint8_t startCode[] =
	{
		0x00, 0x00, 0x00, 0x01
	};

	write(startCode, sizeof(startCode));
}

bool H264Converter::write_codec_private(const std::vector<uint8_t>& codecPrivate)
{
	if (codecPrivate.size() < 7)
	{
		return false;
	}

	if (codecPrivate[0] != 1)
	{
		return false;
	}

	const uint8_t lengthSizeMinusOne = codecPrivate[4] & 0x03;

	m_nal_length_size = lengthSizeMinusOne + 1;

	if ((m_nal_length_size != 1) &&
	    (m_nal_length_size != 2) &&
	    (m_nal_length_size != 4))
	{
		return false;
	}

	size_t pos = 5;

	/* number SPS. */
	const uint8_t numSps = codecPrivate[pos++] & 0x1f;

	for (uint8_t i = 0; i < numSps; i++)
	{
		if (pos + 2 > codecPrivate.size())
		{
			return false;
		}

		const size_t size =
			(static_cast<size_t>(codecPrivate[pos]) << 8) |
			static_cast<size_t>(codecPrivate[pos + 1]);

		pos += 2;

		if (pos + size > codecPrivate.size())
		{
			return false;
		}

		write_start_code();
		write(&codecPrivate[pos], size);
		pos += size;
	}

	if (pos >= codecPrivate.size())
	{
		return false;
	}

	/* number PPS. */
	const uint8_t numPps = codecPrivate[pos++];

	for (uint8_t i = 0; i < numPps; ++i)
	{
		if (pos + 2 > codecPrivate.size())
		{
			return false;
		}

		const size_t size =
			(static_cast<size_t>(codecPrivate[pos]) << 8) |
			static_cast<size_t>(codecPrivate[pos + 1]);

		pos += 2;

		if (pos + size > codecPrivate.size())
		{
			return false;
		}

		write_start_code();
		write(&codecPrivate[pos], size);

		pos += size;
	}

	return true;
}

size_t H264Converter::process_data(const std::vector<uint8_t>& data, const FrameInfo&)
{
	if (!m_codec_private_written)
	{
		write_codec_private(m_track_info.codecPrivate);
		m_codec_private_written = true;
	}

	size_t offset = 0;
	size_t written = 0;

	while (offset < data.size())
	{
		if (data.size() - offset < m_nal_length_size)
		{
			return written;
		}

		uint32_t nalSize = 0;

		for (uint8_t i = 0; i < m_nal_length_size; ++i)
		{
			nalSize = (nalSize << 8) | data[offset + i];
		}

		offset += m_nal_length_size;

		if (nalSize > data.size() - offset)
		{
			return written;
		}

		write_start_code();
		write(&data[offset], nalSize);

		written += 4 + nalSize;

		offset += nalSize;
	}

	return written;
}
