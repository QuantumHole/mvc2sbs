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

TrackConverter::TrackConverter(const TrackInfo& track_info, const bool defer_output_configuration) :
	m_track_info(track_info),
	m_data_buffer(),
	m_mutex(),
	m_thread(),
	m_data_available(),
	m_keep_running(true),
	m_mux_track_id(TrackMuxer::instance().add_live_track(track_info, !defer_output_configuration)),
	m_output_configured(!defer_output_configuration)
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

void TrackConverter::run_loop(void)
{
	std::stringstream s;

	s << "Track " << m_track_info.trackNumber
	  << " -> " << m_track_info.codecId
	  << ", " << m_track_info.language << std::endl;
	std::cout << s.str();
	s.str("");

	while (true)
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
			try
			{
				processed = process_data(block.data, block.info);
			}
			catch (const std::exception& error)
			{
				std::cerr << "Track " << m_track_info.trackNumber
				          << " processing failed: " << error.what() << std::endl;
				processed = 0;
				m_keep_running = false;
			}
			catch (...)
			{
				std::cerr << "Track " << m_track_info.trackNumber
				          << " processing failed with an unknown error" << std::endl;
				processed = 0;
				m_keep_running = false;
			}
		}

		if (processed != block.data.size())
		{
			fail_output_track();
			std::cerr << "block in track "
			          << m_track_info.trackNumber
			          << " at "
			          << (static_cast<double>(block.info.pts) / 1e9)
			          << "s not completed"
			          << std::endl;
		}
	}

	try
	{
		finish_processing();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Track " << m_track_info.trackNumber
		          << " finalization failed: " << error.what() << std::endl;
		fail_output_track();
	}
	catch (...)
	{
		std::cerr << "Track " << m_track_info.trackNumber
		          << " finalization failed with an unknown error" << std::endl;
		fail_output_track();
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

size_t TrackConverter::mux_id(void) const
{
	return m_mux_track_id;
}

void TrackConverter::configure_output_track(
	const AVCodecParameters& parameters,
	const AVRational time_base)
{
	TrackMuxer::instance().configure_live_track(m_mux_track_id, parameters, time_base);
	m_output_configured = true;
}

void TrackConverter::fail_output_track(void)
{
	TrackMuxer::instance().fail_live_track(m_mux_track_id);
}

void TrackConverter::finish_processing(void)
{
	// nothing to do usually.
}

void TrackConverter::finish(void)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_keep_running = false;
	}
	m_data_available.notify_one();

	if (m_thread.joinable())
	{
		m_thread.join();
	}
	else
	{
		try
		{
			finish_processing();
		}
		catch (const std::exception& error)
		{
			std::cerr << "Track " << m_track_info.trackNumber
			          << " finalization failed: " << error.what() << std::endl;
			fail_output_track();
		}
		catch (...)
		{
			std::cerr << "Track " << m_track_info.trackNumber
			          << " finalization failed with an unknown error" << std::endl;
			fail_output_track();
		}
	}

	if (!m_output_configured)
	{
		fail_output_track();
	}

	TrackMuxer::instance().finish_track(m_mux_track_id);
}
