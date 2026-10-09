// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "pgsconverter.h"
#include "trackmuxer.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <png.h>

static std::vector<uint8_t> writeUint32BE(const uint32_t value)
{
	std::vector<uint8_t> data =
	{
		static_cast<uint8_t>((value >> 24) & 0xff),
		static_cast<uint8_t>((value >> 16) & 0xff),
		static_cast<uint8_t>((value >> 8) & 0xff),
		static_cast<uint8_t>(value & 0xff)
	};

	return data;
}

static std::string timestamp(const double seconds)
{
	const uint64_t totalMs =
		static_cast<uint64_t>(seconds * 1000.0 + 0.5);

	const uint64_t hours = totalMs / 3600000;
	const uint64_t minutes = (totalMs / 60000) % 60;
	const uint64_t secs = (totalMs / 1000) % 60;
	const uint64_t millis = totalMs % 1000;

	std::ostringstream result;

	result << std::setfill('0')
	       << std::setw(2) << hours << ":"
	       << std::setw(2) << minutes << ":"
	       << std::setw(2) << secs << "."
	       << std::setw(3) << millis;

	return result.str();
}

static bool saveRGBA32AsPNG(
	const std::string& filename,
	const std::vector<uint32_t>& pixels,
	const size_t width,
	const size_t height)
{
	if ((width == 0) || (height == 0))
	{
		return false;
	}

	const size_t expectedSize = static_cast<size_t>(width) * height;

	if (pixels.size() != expectedSize)
	{
		return false;
	}

	// Ausgabeverzeichnis ermitteln und bei Bedarf anlegen.
	try
	{
		const std::filesystem::path outputPath(filename);
		const std::filesystem::path outputDirectory = outputPath.parent_path();

		if (!outputDirectory.empty())
		{
			std::filesystem::create_directories(outputDirectory);
		}
	}
	catch (const std::filesystem::filesystem_error&)
	{
		return false;
	}

	FILE* file = fopen(filename.c_str(), "wb");

	if (!file)
	{
		return false;
	}

	png_structp png = png_create_write_struct(
		PNG_LIBPNG_VER_STRING,
		nullptr,
		nullptr,
		nullptr);

	if (!png)
	{
		fclose(file);
		return false;
	}

	png_infop info = png_create_info_struct(png);

	if (!info)
	{
		png_destroy_write_struct(&png, nullptr);
		fclose(file);
		return false;
	}

	if (setjmp(png_jmpbuf(png)))
	{
		png_destroy_write_struct(&png, &info);
		fclose(file);
		return false;
	}

	png_init_io(png, file);

	png_set_IHDR(
		png,
		info,
		static_cast<uint32_t>(width),
		static_cast<uint32_t>(height),
		8,                       // Bits pro Kanal
		PNG_COLOR_TYPE_RGBA,
		PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_DEFAULT,
		PNG_FILTER_TYPE_DEFAULT);

	png_write_info(png, info);

	// libpng erwartet pro Zeile zusammenhängende RGBA-Bytes.
	std::vector<uint8_t> row(
		static_cast<size_t>(width) * 4);

	for (uint32_t y = 0; y < height; ++y)
	{
		for (uint32_t x = 0; x < width; ++x)
		{
			const uint32_t argb =
				pixels[static_cast<size_t>(y) * width + x];

			const size_t offset =
				static_cast<size_t>(x) * 4;

			// Annahme: 0xAARRGGBB
			row[offset + 0] = static_cast<uint8_t>(
				(argb >> 16) & 0xff); // R

			row[offset + 1] = static_cast<uint8_t>(
				(argb >> 8) & 0xff);  // G

			row[offset + 2] = static_cast<uint8_t>(
				argb & 0xff);         // B

			row[offset + 3] = static_cast<uint8_t>(
				(argb >> 24) & 0xff); // A
		}

		png_write_row(png, row.data());
	}

	png_write_end(png, nullptr);
	png_destroy_write_struct(&png, &info);
	fclose(file);

	return true;
}

PGSConverter::PGSConverter(const std::string& input_file_name, const TrackInfo& info) :
	TrackConverter(input_file_name, info),
	m_save_images(false),
	m_pgs_stream(),
	m_output_srt(input_file_name + "." + std::to_string(info.trackNumber) + "." + info.language + ".srt"),
	m_ocr(info.language),
	m_line_index(0)
{
}

PGSConverter::~PGSConverter(void)
{
	m_output_srt.flush();
	m_output_srt.close();
}

void PGSConverter::append_ocr(const PGSStream::PGSFrame& frame)
{
	const std::string text = m_ocr.read_text(frame.pixels, frame.size.x, frame.size.y);

	if (text.empty())
	{
		const std::string filename = "unreadable." + track_info().language + "/" + timestamp(frame.time.s) + "-" + timestamp(frame.time.t) + ".png";
		saveRGBA32AsPNG(filename, frame.pixels, frame.size.x, frame.size.y);
	}

	if (m_save_images)
	{
		const std::string filename = "subtitles." + track_info().language + "/" + timestamp(frame.time.s) + "-" + timestamp(frame.time.t) + ".png";
		saveRGBA32AsPNG(filename, frame.pixels, frame.size.x, frame.size.y);
	}

	if (!text.empty())
	{
		// passed++;
		m_output_srt << m_line_index << std::endl;
		m_output_srt << timestamp(frame.time.s) << " --> " << timestamp(frame.time.t) << std::endl;
		m_output_srt << text << std::endl << std::endl;
		m_line_index++;
	}
}

size_t PGSConverter::process_data(const std::vector<uint8_t>& data, const FrameInfo& info)
{
	if (data.empty())
	{
		std::cerr << "Leerer PGS-Frame in language " << track_info().language << std::endl;
		return 0;
	}

	std::vector<uint8_t> pgsdata;
	const size_t size = data.size();
	size_t offset = 0;
	TrackMuxer& muxer = TrackMuxer::instance();

	while (offset < size)
	{
		if (size - offset < 3)
		{
			std::cerr << "invalid PGS segment" << std::endl;
			return offset;
		}

		const uint8_t segmentType = data[offset];

		const uint16_t segmentLength = static_cast<uint16_t>(
			(static_cast<uint16_t>(data[offset + 1]) << 8) |
			static_cast<uint16_t>(data[offset + 2]));

		const size_t segmentSize = 3 + static_cast<size_t>(segmentLength);

		if (segmentSize > size - offset)
		{
			std::cerr
			    << "    PGS-Segment außerhalb des Buffers: "
			    << "type=0x"
			    << std::hex
			    << static_cast<unsigned int>(segmentType)
			    << std::dec
			    << ", length="
			    << segmentLength
			    << std::endl;

			return offset;
		}

		std::vector<uint8_t> value = { 0x50, 0x47 };     // "PG"
		pgsdata.insert(pgsdata.end(), value.begin(), value.end());

		// PTS are sampled with 90kHz
		const uint32_t pts = static_cast<uint32_t>(info.pts * 90000 / 1000000000);
		value = writeUint32BE(pts);
		pgsdata.insert(pgsdata.end(), value.begin(), value.end());
		value = writeUint32BE(0);
		pgsdata.insert(pgsdata.end(), value.begin(), value.end());

		std::vector<uint8_t> frame_data(
			data.begin() + static_cast<ptrdiff_t>(offset),
			data.begin() + static_cast<ptrdiff_t>(offset) + static_cast<ptrdiff_t>(segmentSize));

		pgsdata.insert(pgsdata.end(), frame_data.begin(), frame_data.end());

		muxer.push(
			mux_id(),
			frame_data,
			static_cast<int64_t>(info.pts / 1e6),
			0,
			0,
			0);

		offset += segmentSize;
	}

	m_pgs_stream.push(pgsdata);

	while (m_pgs_stream.hasNextFrame())
	{
		PGSStream::PGSFrame frame = m_pgs_stream.getNextFrame();
		append_ocr(frame);
		const std::vector<uint8_t> payload = m_pgs_stream.saveFrame(frame, true);
		write(payload.data(), payload.size());
	}

	return offset;
}
