#ifndef TRACKCONVERTER_H
#define TRACKCONVERTER_H

#include <string>
#include <mutex>
#include <vector>
#include <thread>
#include <fstream>

class TrackConverter
{
	public:
		struct BlockData
		{
			uint64_t timestamp;         // in nanoseconds
			std::vector<uint8_t> data;  // block data

			BlockData(const uint64_t ts, const std::vector<uint8_t>& d) :
				timestamp(ts),
				data(d)
			{
			}

			BlockData(const uint64_t ts, const uint8_t * d, const size_t size) :
				timestamp(ts),
				data(d, d + size)
			{
			}
		};

		explicit TrackConverter(const std::string& input_file_name, const size_t track_number, const std::string& track_id);
		virtual ~TrackConverter(void);
		const std::string& track_id(void) const;
		void add_data(const std::vector<uint8_t>& data, const uint64_t timestamp);
		void add_data(const uint8_t* data, const size_t size, const uint64_t timestamp);

	protected:
		void write(const uint8_t* data, const size_t size);
		virtual const std::string output_extension(void) const = 0;
		virtual size_t process_data(const std::vector<uint8_t>& data, const uint64_t timestamp) = 0;

	private:
		const std::string m_input_file_name;
		const size_t m_track_number;
		const std::string m_track_id;
		std::ofstream m_output;
		std::vector<BlockData> m_data_buffer;
		std::mutex m_mutex;
		std::thread m_thread;
		bool m_keep_running;

		std::string output_file_name(void) const;
		void run_loop(void);
		static void runner(TrackConverter* instance);
};

#endif
