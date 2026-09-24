#ifndef TRACKCONVERTER_H
#define TRACKCONVERTER_H

#include <string>
#include <mutex>
#include <vector>
#include <thread>

class TrackConverter
{
    private:
        const std::string m_track_id;
        const std::string m_output_file_name;
        std::mutex m_mutex;
        std::vector<uint8_t> m_data_buffer;
        std::thread* m_thread;

        TrackConverter(const TrackConverter&);
        TrackConverter& operator=(const TrackConverter&);
        static void runner(TrackConverter* instance);

    protected:
        // virtual void process_data(void) = 0;

    public:
        explicit TrackConverter(const std::string& input_file_name, const std::string& track_id);
        virtual ~TrackConverter(void);
        const std::string& track_id(void) const;
        void add_data(const std::vector<uint8_t>& data);
        void add_data(const uint8_t* data, const size_t size);
};

#endif
