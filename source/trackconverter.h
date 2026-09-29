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
		struct TrackInfo
		{
			uint64_t trackNumber = 0;
			std::string codecId = "";
			std::vector<uint8_t> codecPrivate = {};
			uint64_t width = 0;
			uint64_t height = 0;
			uint64_t codecDelay = 0;        // ns
			uint64_t defaultDuration = 0;   // ns
			uint64_t timestampScale = 1'000'000;
			std::string language = "und";   // undefined
		};

		struct FrameInfo
		{
			uint64_t pts = 0;           // nanoseconds
			uint64_t dts = 0;           // nanoseconds
			uint64_t duration = 0;      // nanoseconds
			uint32_t index = 0;
			bool keyframe = false;
		};

		struct BlockData
		{
			FrameInfo info;
			std::vector<uint8_t> data;  // block data

			BlockData(const FrameInfo& i, const std::vector<uint8_t>& d) :
				info(i),
				data(d)
			{
			}

			BlockData(const FrameInfo& i, const uint8_t * d, const size_t size) :
				info(i),
				data(d, d + size)
			{
			}
		};

		explicit TrackConverter(const std::string& input_file_name, const TrackInfo& trackinfo);
		virtual ~TrackConverter(void);
		const TrackInfo& track_info(void) const;
		void add_data(const std::vector<uint8_t>& data, const FrameInfo& frameinfo);
		void add_data(const uint8_t* data, const size_t size, const FrameInfo& frameinfo);

	protected:
		const TrackInfo m_track_info;

		void write(const uint8_t* data, const size_t size);
		virtual const std::string output_extension(void) const = 0;
		virtual size_t process_data(const std::vector<uint8_t>& data, const FrameInfo& info) = 0;

	private:
		const std::string m_input_file_name;

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
