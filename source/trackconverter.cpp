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
#include <unordered_map>

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
	static const std::unordered_map<std::string, std::string>  mapping = {
		{"S_HDMV/PGS",       "sup"    },
		{"S_TEXT/UTF8",      "srt"    },
		{"S_TEXT/ASS",       "ass"    },
		{"S_TEXT/SSA",       "ass"    },
		{"S_VOBSUB",         "vobsub" },
		{"S_DVBSUB",         "dvbsub" },
		{"S_TEXT/WEBVTT",    "webvtt" },
		{"A_AC3",            "ac3"    },
		{"A_EAC3",           "eac3"   },
		{"A_DTS",            "dts"    },
		{"A_AAC",            "aac"    },
		{"A_MPEG/L3",        "mp3"    },
		{"A_MPEG/L2",        "mp2"    },
		{"A_FLAC",           "flac"   },
		{"A_OPUS",           "ogg"    },
		{"A_VORBIS",         "ogg"    },
		{"V_MPEG4/ISO/AVC",  "h264"   },
		{"V_MPEGH/ISO/HEVC", "hevc"   },
		{"V_AV1",            "av1"    },
		{"V_VP8",            "vp8"    },
		{"V_VP9",            "vp9"    }
	};

	const std::unordered_map<std::string, std::string>::const_iterator it = mapping.find(codec_id);

	return it != mapping.end() ? it->second : "none";
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
	finish();
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

size_t TrackConverter::mux_id(void) const
{
	return m_mux_track_id;
}

void TrackConverter::write(const uint8_t* data, const size_t size)
{
	m_output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));

	if (!m_output)
	{
		throw std::runtime_error("failed writing SUP file");
	}
}

void TrackConverter::finish(void)
{
	m_keep_running = false;
	m_data_available.notify_one();

	if (m_thread.joinable())
	{
		m_thread.join();
	}

	if (m_output.good())
	{
		m_output.flush();
		m_output.close();
	}

	TrackMuxer::instance().finish_track(m_mux_track_id);
}
