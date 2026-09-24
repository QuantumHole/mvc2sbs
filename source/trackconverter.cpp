#include "trackconverter.h"

#include <chrono>
#include <iostream>

TrackConverter::TrackConverter(const std::string& input_file_name, const std::string& track_id) :
    m_track_id(track_id),
    m_output_file_name(input_file_name),
    m_mutex(),
    m_data_buffer(),
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
}

const std::string& TrackConverter::track_id(void) const
{
    return m_track_id;
}

void TrackConverter::runner(TrackConverter* instance)
{
    std::cout << "starting thread for track ID " << instance->m_track_id << std::endl;
    while (instance->m_keep_running)
    {
        instance->m_mutex.lock();
        const size_t presize = instance->m_data_buffer.size();
        instance->process_data();
        const size_t postsize = instance->m_data_buffer.size();
        instance->m_mutex.unlock();

        if (presize == postsize)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void TrackConverter::add_data(const std::vector<uint8_t>& data)
{
    // const std::lock_guard<std::mutex> lock(m_mutex);
    m_mutex.lock();
    m_data_buffer.insert(m_data_buffer.end(), data.begin(), data.end());
    m_mutex.unlock();
    if (m_thread == nullptr)
    {
        m_thread = new std::thread(runner, this);
    }
}

void TrackConverter::add_data(const uint8_t* data, const size_t size)
{
    // const std::lock_guard<std::mutex> lock(m_mutex);
    m_mutex.lock();
    m_data_buffer.insert(m_data_buffer.end(), data, data + size);
    m_mutex.unlock();
    if (m_thread == nullptr)
    {
        m_thread = new std::thread(runner, this);
    }
}

void TrackConverter::process_data(void)
{

}