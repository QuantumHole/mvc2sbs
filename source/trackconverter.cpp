#include "trackconverter.h"

#include <chrono>
#include <iostream>
#include <sstream>

TrackConverter::TrackConverter(const std::string& input_file_name, const TrackInfo& track_info) :
	m_track_info(track_info),
	m_input_file_name(input_file_name),
	m_output(),
	m_data_buffer(),
	m_mutex(),
	m_thread(),
	m_keep_running(true)
{
}

TrackConverter::~TrackConverter(void)
{
	m_keep_running = false;

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
	       "_track" +
	       std::to_string(m_track_info.trackNumber) +
	       output_extension();
}

void TrackConverter::run_loop(void)
{
	std::stringstream s;

	const std::string name = output_file_name();

	s << "Track " << m_track_info.trackNumber << " -> " << name << std::endl;
	std::cout << s.str();
	s.str("");

	m_output.open(name, std::ios::binary | std::ios::out | std::ios::trunc);

	if (!m_output)
	{
		throw std::runtime_error("Kann Datei nicht öffnen: " + name);
	}

	while (m_keep_running)
	{
		m_mutex.lock();

		if (m_data_buffer.size() == 0)
		{
			m_mutex.unlock();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}
		const BlockData& block = *m_data_buffer.begin();
		const size_t processed = process_data(block.data, block.info);

		if (processed != block.data.size())
		{
			std::cout << "block at " << block.info.pts << "ns not completed" << std::endl;
		}
		m_data_buffer.erase(m_data_buffer.begin(), m_data_buffer.begin() + 1);
		m_mutex.unlock();
	}
	std::cout << "thread finished for track " << m_track_info.trackNumber << std::endl;
}

void TrackConverter::runner(TrackConverter* instance)
{
	instance->run_loop();
}

void TrackConverter::add_data(const std::vector<uint8_t>& data, const FrameInfo& frameinfo)
{
	// const std::lock_guard<std::mutex> lock(m_mutex);
	m_mutex.lock();
	// m_data_buffer.insert(m_data_buffer.end(), data.begin(), data.end());
	m_data_buffer.emplace_back(frameinfo, data);
	m_mutex.unlock();

	if (!m_thread.joinable())
	{
		m_thread = std::thread(runner, this);
	}
}

void TrackConverter::add_data(const uint8_t* data, const size_t size, const FrameInfo& frameinfo)
{
	// const std::lock_guard<std::mutex> lock(m_mutex);
	m_mutex.lock();
	m_data_buffer.emplace_back(frameinfo, data, size);
	m_mutex.unlock();

	if (!m_thread.joinable())
	{
		m_thread = std::thread(runner, this);
	}
}

void TrackConverter::write(const uint8_t* data, const size_t size)
{
	m_output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));

	if (!m_output)
	{
		throw std::runtime_error("Fehler beim Schreiben der SUP-Datei");
	}
}
