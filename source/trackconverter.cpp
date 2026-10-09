// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "trackconverter.h"
#include "trackmuxer.h"

#include <chrono>
#include <iostream>
#include <sstream>

#include <string>
#include <stdexcept>

/**
 * Convert a Matroska CodecID to the FFmpeg input format name
 * used by av_find_input_format().
 * This is also used to determine the file extension for
 * elementary streams containing encoded data.
 *
 * Examples:
 *
 *   S_HDMV/PGS          -> "sup"
 *   S_TEXT/UTF8         -> "srt"
 *   S_TEXT/ASS          -> "ass"
 *   A_AC3               -> "ac3"
 *   A_EAC3              -> "eac3"
 *   A_DTS               -> "dts"
 *   V_MPEG4/ISO/AVC     -> "h264"
 *   V_MPEGH/ISO/HEVC    -> "hevc"
 */
static std::string ffmpeg_format_from_matroska_codec_id(const std::string& codec_id)
{
	if (codec_id == "S_HDMV/PGS")
	{
		return "sup";
	}

	if (codec_id == "S_TEXT/UTF8")
	{
		return "srt";
	}

	if ((codec_id == "S_TEXT/ASS") ||
	    (codec_id == "S_TEXT/SSA"))
	{
		return "ass";
	}

	if (codec_id == "S_VOBSUB")
	{
		return "vobsub";
	}

	if (codec_id == "S_DVBSUB")
	{
		return "dvbsub";
	}

	if (codec_id == "S_TEXT/WEBVTT")
	{
		return "webvtt";
	}

	if (codec_id == "A_AC3")
	{
		return "ac3";
	}

	if (codec_id == "A_EAC3")
	{
		return "eac3";
	}

	if (codec_id == "A_DTS")
	{
		return "dts";
	}

	if (codec_id == "A_AAC")
	{
		return "aac";
	}

	if (codec_id == "A_MPEG/L3")
	{
		return "mp3";
	}

	if (codec_id == "A_MPEG/L2")
	{
		return "mp2";
	}

	if (codec_id == "A_FLAC")
	{
		return "flac";
	}

	if (codec_id == "A_OPUS")
	{
		return "ogg";
	}

	if (codec_id == "A_VORBIS")
	{
		return "ogg";
	}

	if (codec_id == "V_MPEG4/ISO/AVC")
	{
		return "h264";
	}

	if (codec_id == "V_MPEGH/ISO/HEVC")
	{
		return "hevc";
	}

	if (codec_id == "V_AV1")
	{
		return "av1";
	}

	if (codec_id == "V_VP8")
	{
		return "vp8";
	}

	if (codec_id == "V_VP9")
	{
		return "vp9";
	}

	throw std::invalid_argument(
			  "Unsupported Matroska CodecID: " + codec_id);
}

TrackConverter::TrackConverter(const std::string& input_file_name, const TrackInfo& track_info) :
	m_input_file_name(input_file_name),
	m_track_info(track_info),
	m_output(),
	m_data_buffer(),
	m_mutex(),
	m_thread(),
	m_data_available(),
	m_keep_running(true),
	m_mux_track_id(TrackMuxer::instance().add_live_track(track_info))
{
}

TrackConverter::~TrackConverter(void)
{
	m_keep_running = false;
	m_data_available.notify_one();

	if (m_thread.joinable())
	{
		m_thread.join();
	}
	m_output.flush();
	m_output.close();
}

const TrackConverter::TrackInfo& TrackConverter::track_info(void) const
{
	return m_track_info;
}

std::string TrackConverter::output_file_name(void) const
{
	const size_t slash = m_input_file_name.find_last_of("/\\");
	const size_t start = (slash == std::string::npos) ? 0 : (slash + 1);
	const size_t dot = m_input_file_name.find_last_of('.');
	const size_t ext = ((dot != std::string::npos) && (dot >= start)) ? dot : m_input_file_name.size();
	const std::string directory = (slash == std::string::npos) ? std::string() : m_input_file_name.substr(0, slash + 1);

	return directory +
	       m_input_file_name.substr(start, ext - start) +
	       ".track" +
	       std::to_string(m_track_info.trackNumber) +
	       "." + m_track_info.language +
	       "." + ffmpeg_format_from_matroska_codec_id(m_track_info.codecId);
}

void TrackConverter::run_loop(void)
{
	std::stringstream s;

	const std::string name = output_file_name();
	const std::string ffmpeg_id = ffmpeg_format_from_matroska_codec_id(m_track_info.codecId);

	(void)ffmpeg_id;
	// TrackMuxer::instance().add_file(name,  0, ffmpeg_id, m_track_info.language, "");

	s << "Track " << m_track_info.trackNumber << " -> " << name << std::endl;
	std::cerr << s.str();
	s.str("");

	m_output.open(name, std::ios::binary | std::ios::out | std::ios::trunc);

	if (!m_output)
	{
		throw std::runtime_error("failed opening file: " + name);
	}

	while (m_keep_running)
	{
		BlockData block;
		size_t processed;
		{
			std::unique_lock<std::mutex> lock(m_mutex);

			m_data_available.wait(lock, [this] {
				return !m_data_buffer.empty() || !m_keep_running;
			});

			if (!m_keep_running && m_data_buffer.empty())
			{
				break;
			}

			block = std::move(m_data_buffer.front());
			m_data_buffer.pop_front();

			// keep this function inside the locked section.
			// Otherwise, this buffer gets filled with uncompressed
			// frames, waiting to be re-compressed and saved to disk.
			processed = process_data(block.data, block.info);
		}

		if (processed != block.data.size())
		{
			std::cerr << "block in track "
			          << m_track_info.trackNumber
			          << " at "
			          << (static_cast<double>(block.info.pts) / 1e9)
			          << "s not completed"
			          << std::endl;
		}
	}
}

void TrackConverter::runner(TrackConverter* instance)
{
	instance->run_loop();
}

void TrackConverter::add_data(const std::vector<uint8_t>& data, const FrameInfo& frameinfo)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_data_buffer.emplace_back(frameinfo, data);
	}

	m_data_available.notify_one();

	if (!m_thread.joinable())
	{
		m_thread = std::thread(runner, this);
	}
}

void TrackConverter::add_data(const uint8_t* data, const size_t size, const FrameInfo& frameinfo)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_data_buffer.emplace_back(frameinfo, data, size);
	}

	m_data_available.notify_one();

	if (!m_thread.joinable())
	{
		m_thread = std::thread(runner, this);
	}
}

std::ofstream& TrackConverter::output_stream(void)
{
	return m_output;
}

void TrackConverter::write(const uint8_t* data, const size_t size)
{
	m_output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));

	if (!m_output)
	{
		throw std::runtime_error("failed writing SUP file");
	}
}
