// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef TRACKCONVERTER_H
#define TRACKCONVERTER_H

#include <string>
#include <mutex>
#include <vector>
#include <thread>
#include <fstream>
#include <deque>
#include <condition_variable>

extern "C" {
#include <libavcodec/avcodec.h>
}

/** Abstract class for receiving data of one MKV track and processing it in
 * arbitrary ways.
 */
class TrackConverter
{
	public:
		/** meta data about the track to be prcessed. */
		struct TrackInfo
		{
			uint64_t trackNumber = 0;                   ///< matroska track number
			std::string codecId = "";                   ///< matroska track ID
			std::vector<uint8_t> codecPrivate = {};
			uint64_t width = 0;
			uint64_t height = 0;
			uint64_t codecDelay = 0;                   // nanoseconds
			uint64_t defaultDuration = 0;              // nanoseconds
			uint64_t timestampScale = 1'000'000;
			std::string language = "und";              ///< track language, default "undefined"
			std::string title = "";                    ///< track title
		};

		/** metadata about a frame or block of frames, mostly timing information. */
		struct FrameInfo
		{
			uint64_t pts = 0;           // nanoseconds
			uint64_t dts = 0;           // nanoseconds
			uint64_t duration = 0;      // nanoseconds
			uint32_t index = 0;
			bool keyframe = false;
		};

		/** container for block data and meta data. */
		struct BlockData
		{
			FrameInfo info;             ///< meta data about the current block with one or more frames (cf. lacing).
			std::vector<uint8_t> data;  ///< block data read directly from the track

			/** default constructor. */
			BlockData(void) :
				info(),
				data()
			{
			}

			/** constructor for vector data.
			 * @param i frame information.
			 * @param d block data for one or more frames.
			 */
			BlockData(const FrameInfo& i, const std::vector<uint8_t>& d) :
				info(i),
				data(d)
			{
			}

			/** constructor for classic pointer and size data.
			 * @param i frame information.
			 * @param d pointer to the block data for one or more frames.
			 * @param size length of the block data.
			 */
			BlockData(const FrameInfo& i, const uint8_t * d, const size_t size) :
				info(i),
				data(d, d + size)
			{
			}
		};

		/** constructor.
		 * @param trackinfo information about the assigned track.
		 */
		explicit TrackConverter(const TrackInfo& trackinfo);
		TrackConverter(const TrackInfo& trackinfo, bool defer_output_configuration);

		/** destructor. */
		virtual ~TrackConverter(void);

		/** retrieve the track information.
		 * @return reference to the track information.
		 */
		const TrackInfo& track_info(void) const [[clang::lifetimebound]];

		/** add a bunch of data bytes.
		 * @param data put data bytes into input queue.
		 * @param frameinfo associated meta data for the queued block of data.
		 */
		void add_data(const std::vector<uint8_t>& data, const FrameInfo& frameinfo);

		/** add a bunch of data bytes.
		 * @param data pointer to data bytes for the input queue.
		 * @param size number of data bytes to be put into input queue.
		 * @param frameinfo associated meta data for the queued block of data.
		 */
		void add_data(const uint8_t* data, const size_t size, const FrameInfo& frameinfo);

		/** notify track converter that no more data is comming in. */
		void finish(void);

	protected:
		/** output muxer track ID.
		 * @return muxer track ID.
		 */
		size_t mux_id(void) const;
		void configure_output_track(const AVCodecParameters& parameters, AVRational time_base);
		void fail_output_track(void);

		/** process packets of data.
		 * @param data one bunch of data to be processed. Data may be cut at arbitrary positions into packets.
		 * @param info meta data for the current frame, that may be contained in the current packet of data.
		 * @return number of processed bytes. This must be the size of data, everything else is considered an error.
		 */
		virtual size_t process_data(const std::vector<uint8_t>& data, const FrameInfo& info) = 0;

		/** called when processing loop has finished. */
		virtual void finish_processing(void);

	private:
		const TrackInfo m_track_info;              ///< track meta data.
		std::deque<BlockData> m_data_buffer;       ///< queue for the read input packets from input file.
		std::mutex m_mutex;                        ///< mutex to syncronize communication between thread and main program.
		std::thread m_thread;                      ///< thread variable for the thread running process_data().
		std::condition_variable m_data_available;  ///< condition variable for signalling when more input data for the thread is available.
		bool m_keep_running;                       ///< flag for stopping and exiting the thread.
		const size_t m_mux_track_id;               ///< track ID for the output muxer.
		bool m_output_configured;

		/** thread loop for the converter.
		 * This loop regularly checks the input queue and calls process_data().
		 */
		void run_loop(void);

		/** static method to start the thread loop for the converter.
		 * @param instance instance for which to run the thread loop.
		 */
		static void runner(TrackConverter* instance);
};

#endif
