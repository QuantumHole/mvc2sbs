#include "trackconverter.h"

#include <chrono>
#include <iostream>

TrackConverter::TrackConverter(const std::string& input_file_name, const size_t track_number, const std::string& track_id) :
    m_input_file_name(input_file_name),
    m_track_number(track_number),
    m_track_id(track_id),
    m_output(),
    m_data_buffer(),
    m_mutex(),
    m_thread(nullptr),
    m_keep_running(true)
{
}

TrackConverter::~TrackConverter(void)
{
    m_keep_running = false;
    if (m_thread)
    {
        m_thread->join();
        delete m_thread;
    }
    m_output.flush();
    m_output.close();

}

const std::string& TrackConverter::track_id(void) const
{
    return m_track_id;
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
	       std::to_string(m_track_number) +
	       ".sup";
}

void TrackConverter::runner(TrackConverter* instance)
{
    std::cout << "starting thread for track ID " << instance->m_track_id << std::endl;

    const std::string name = instance->output_file_name();
    std::cout << "Track " << instance->m_track_number << " -> " << name << std::endl;
    instance->m_output.open(name, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!instance->m_output)
    {
        throw std::runtime_error("Kann Datei nicht öffnen: " + name);
    }

    while (instance->m_keep_running)
    {
        instance->m_mutex.lock();
        if (instance->m_data_buffer.size() == 0)
        {
            instance->m_mutex.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        const FrameData& frame = *instance->m_data_buffer.begin();
        const size_t processed = instance->process_data(frame.data, frame.timestamp);
        if (processed != frame.data.size())
        {
        }
        instance->m_data_buffer.erase(instance->m_data_buffer.begin(), instance->m_data_buffer.begin() + 1);
        instance->m_mutex.unlock();

        // if (processed == 0)
        // {
        // }
    }
}

void TrackConverter::add_data(const std::vector<uint8_t>& data, const uint64_t timestamp)
{
    // const std::lock_guard<std::mutex> lock(m_mutex);
    m_mutex.lock();
    // m_data_buffer.insert(m_data_buffer.end(), data.begin(), data.end());
    m_data_buffer.emplace_back(timestamp, data);
    m_mutex.unlock();
    if (m_thread == nullptr)
    {
        m_thread = new std::thread(runner, this);
    }
}

void TrackConverter::add_data(const uint8_t* data, const size_t size, const uint64_t timestamp)
{
    // const std::lock_guard<std::mutex> lock(m_mutex);
    m_mutex.lock();
    m_data_buffer.emplace_back(timestamp, data, size);
    m_mutex.unlock();
    if (m_thread == nullptr)
    {
        m_thread = new std::thread(runner, this);
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
