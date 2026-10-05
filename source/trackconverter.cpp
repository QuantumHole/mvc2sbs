// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "trackconverter.h"

#include <chrono>
#include <iostream>
#include <sstream>

TrackConverter::TrackConverter(const std::string& input_file_name, const TrackInfo& track_info) :
	m_input_file_name(input_file_name),
	m_track_info(track_info),
	m_output(),
	m_data_buffer(),
	m_mutex(),
	m_thread(),
	m_data_available(),
	m_keep_running(true)
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
	       output_extension();
}

void TrackConverter::run_loop(void)
{
	std::stringstream s;

	const std::string name = output_file_name();

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
