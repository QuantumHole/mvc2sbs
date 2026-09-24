#include "trackconverter.h"

#include <iostream>

TrackConverter::TrackConverter(const std::string& input_file_name, const std::string& track_id) :
    m_track_id(track_id),
    m_output_file_name(input_file_name),
    m_mutex(),
    m_data_buffer(),
    m_thread(nullptr)
{
}

TrackConverter::~TrackConverter(void)
{
    if (m_thread)
    {
        m_thread->join();
        delete m_thread;
    }
}

const std::string& TrackConverter::track_id(void) const
{
    return m_track_id;
}

void TrackConverter::runner(TrackConverter* instance)
{
    std::cout << "starting thread for track ID " << instance->m_track_id << std::endl;
}

void TrackConverter::add_data(const std::vector<uint8_t>& data)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_data_buffer.insert(m_data_buffer.end(), data.begin(), data.end());
    if (m_thread == nullptr)
    {
        // m_thread = new std::thread(runner, this);
    }
}

void TrackConverter::add_data(const uint8_t* data, const size_t size)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_data_buffer.insert(m_data_buffer.end(), data, data + size);
    if (m_thread == nullptr)
    {
        // m_thread = new std::thread(runner, this);
    }
}
