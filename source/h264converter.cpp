// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "h264converter.h"

#include <iostream>

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>
#include <algorithm>

H264Converter::H264Converter(const TrackInfo& info) :
	TrackConverter(info, true),
	m_nal_length_size(0),
	m_codec_private_written(false),
	m_mvc2sbs(
		mux_id(),
		[this](const AVCodecParameters& parameters, const AVRational time_base) {
	configure_output_track(parameters, time_base);
},
		[this]() {
	fail_output_track();
})
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

void H264Converter::finish_processing(void)
{
	if (!m_mvc2sbs.finish())
	{
		fail_output_track();
	}
}

void H264Converter::write_start_code(std::vector<uint8_t>& data)
{
	static constexpr uint8_t startCode[] =
	{
		0x00, 0x00, 0x00, 0x01
	};

	data.insert(data.end(), startCode, startCode + sizeof(startCode));
}

bool H264Converter::write_codec_private(const std::vector<uint8_t>& codecPrivate, std::vector<uint8_t>& data)
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

	// lokal buffer to avoid lifetime safety error from clang
	// content must only be appended once to data.
	std::vector<uint8_t> converted;
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

		write_start_code(converted);
		converted.insert(converted.end(), &codecPrivate[pos], &codecPrivate[pos + size]);
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

		write_start_code(converted);
		converted.insert(converted.end(), &codecPrivate[pos], &codecPrivate[pos + size]);

		pos += size;
	}

	data.insert(data.end(), converted.begin(), converted.end());
	return true;
}

bool H264Converter::write_mvc_codec_private(
	const std::vector<uint8_t>& codecPrivate,
	std::vector<uint8_t>& output)
{
	/*
	 * In diesem Matroska-CodecPrivate liegt der MVC-Block als:
	 *
	 *   00 00 00 01
	 *   18
	 *   'm' 'v' 'c' 'C'
	 *   MVCDecoderConfigurationRecord
	 *
	 * Der MVCDecoderConfigurationRecord ist strukturell
	 * ähnlich dem AVCDecoderConfigurationRecord:
	 *
	 *   version
	 *   profile
	 *   compatibility
	 *   level
	 *   flags/lengthSize
	 *   num SPS
	 *   [SPS length + SPS]...
	 *   num PPS
	 *   [PPS length + PPS]...
	 */

	static const uint8_t marker[] =
	{
		0x00, 0x00, 0x00, 0x01,
		0x18,
		'm', 'v', 'c', 'C'
	};

	const auto it = std::search(
		codecPrivate.begin(),
		codecPrivate.end(),
		std::begin(marker),
		std::end(marker));

	if (it == codecPrivate.end())
	{
		return true; // kein MVC-Block
	}
	std::size_t pos =
		static_cast<std::size_t>(
			std::distance(codecPrivate.begin(), it))
		+ sizeof(marker);

	const std::size_t size = codecPrivate.size();

	// configurationVersion
	if (pos + 5 > size)
	{
		return false;
	}

	if (codecPrivate[pos++] != 1)
	{
		return false;
	}

	// AVCProfileIndication
	++pos;

	// profile_compatibility
	++pos;

	// AVCLevelIndication
	++pos;

	/*
	 * byte:
	 *
	 * bit 7: complete_representation
	 * bit 6: explicit_au_track
	 * bit 5..2: reserved
	 * bit 1..0: lengthSizeMinusOne
	 */
	const uint8_t flags = codecPrivate[pos++];

	const unsigned lengthSizeMinusOne = flags & 0x03;

	if (lengthSizeMinusOne > 3)
	{
		return false;
	}

	// lokal buffer to avoid lifetime safety error from clang
	// content must only be appended once to data.
	std::vector<uint8_t> converted;

	/*
	 * next Byte:
	 *
	 * bit 7: reserved
	 * bit 6..0: number of SPS
	 */
	const unsigned numSps = codecPrivate[pos++] & 0x7f;

	for (unsigned i = 0; i < numSps; ++i)
	{
		if (pos + 2 > size)
		{
			return false;
		}

		const std::size_t nalSize =
			(static_cast<std::size_t>(codecPrivate[pos]) << 8) |
			static_cast<std::size_t>(codecPrivate[pos + 1]);

		pos += 2;

		if ((nalSize == 0) || (pos + nalSize > size))
		{
			return false;
		}

		write_start_code(converted);
		converted.insert(
			converted.end(),
			codecPrivate.begin() + static_cast<std::ptrdiff_t>(pos),
			codecPrivate.begin() +
			static_cast<std::ptrdiff_t>(pos + nalSize));

		pos += nalSize;
	}

	if (pos >= size)
	{
		return false;
	}

	const unsigned numPps = codecPrivate[pos++];

	for (unsigned i = 0; i < numPps; ++i)
	{
		if (pos + 2 > size)
		{
			return false;
		}

		const std::size_t nalSize =
			(static_cast<std::size_t>(codecPrivate[pos]) << 8) |
			static_cast<std::size_t>(codecPrivate[pos + 1]);

		pos += 2;

		if ((nalSize == 0) || (pos + nalSize > size))
		{
			return false;
		}

		write_start_code(converted);

		converted.insert(
			converted.end(),
			codecPrivate.begin() + static_cast<std::ptrdiff_t>(pos),
			codecPrivate.begin() +
			static_cast<std::ptrdiff_t>(pos + nalSize));

		pos += nalSize;
	}
	output.insert(output.end(), converted.begin(), converted.end());

	return true;
}

size_t H264Converter::process_data(const std::vector<uint8_t>& data, const FrameInfo& info)
{
	if (m_nal_length_size == 0)
	{
		fail_output_track();
		return 0;
	}

	const auto drop_frame = [&data, &info](const char* reason) {
								std::cerr << "H264: skipping damaged frame " << info.index
								          << " at " << (static_cast<double>(info.pts) / 1e9)
								          << "s: " << reason << '\n';
								return data.size();
							};

	std::vector<uint8_t> output_data;
	const bool writing_codec_private = !m_codec_private_written;

	if (writing_codec_private)
	{
		// Normale AVC SPS/PPS.
		if (!write_codec_private(track_info().codecPrivate, output_data))
		{
			std::cerr << "H264: invalid AVC codec private data\n";
			return 0;
		}

		// MVC SPS/PPS einschließlich Subset-SPS.
		if (!write_mvc_codec_private(track_info().codecPrivate, output_data))
		{
			std::cerr << "H264: invalid MVC codec private data\n";
			return 0;
		}
	}

	std::size_t offset = 0;

	while (offset < data.size())
	{
		if (data.size() - offset < m_nal_length_size)
		{
			return drop_frame("truncated NAL length field");
		}

		uint32_t nalSize = 0;

		for (uint8_t i = 0; i < m_nal_length_size; ++i)
		{
			nalSize = (nalSize << 8) |
			          static_cast<uint32_t>(data[offset + i]);
		}

		offset += m_nal_length_size;

		if ((nalSize == 0) ||
		    (static_cast<std::size_t>(nalSize) >
		     data.size() - offset))
		{
			return drop_frame("invalid NAL size");
		}

		/*
		 * Alle NAL-Typen unverändert übernehmen.
		 *
		 * Insbesondere:
		 *   1  AVC slice
		 *   6  SEI
		 *   9  AUD
		 *  14  prefix NAL
		 *  15  subset SPS
		 *  20  MVC slice
		 *
		 * Wir ersetzen ausschließlich das AVCC-Length-Feld
		 * durch einen Annex-B Startcode.
		 */
		write_start_code(output_data);

		output_data.insert(
			output_data.end(),
			data.begin() + static_cast<std::ptrdiff_t>(offset),
			data.begin() +
			static_cast<std::ptrdiff_t>(offset + nalSize));

		offset += nalSize;
	}

	if (!output_data.empty())
	{
		// The MVC video stream is converted into SBS format
		// and re-compressed into H.264 format.
		if (!m_mvc2sbs.feed(output_data))
		{
			return 0;
		}
	}

	if (writing_codec_private)
	{
		m_codec_private_written = true;
	}

	return data.size();
}
