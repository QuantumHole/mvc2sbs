#ifndef TRACKMUXER_H
#define TRACKMUXER_H

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <utility>

extern "C" {
#include <libavformat/avformat.h>
}

#include "trackconverter.h"

/**
 * Muxes a number of input files and/or live tracks into a single
 * Matroska file.
 *
 * File tracks are read by the muxer itself.
 * Live tracks receive encoded AVPacket payloads from producer threads.
 */
class TrackMuxer
{
	private:

		/** structure for pushing data from TrackConverter. */
		struct LivePacket
		{
			std::vector<uint8_t> data;       ///< data for one frame.
			int64_t pts = AV_NOPTS_VALUE;    ///< presentation timestamp.
			int64_t dts = AV_NOPTS_VALUE;    ///< decoding timestamp.
			int64_t duration = 0;            ///< frame display duration.
			int flags = 0;                   ///< additional for the packet flags.
		};

		/** information about one input track (file or stream). */
		struct TrackFile
		{
			std::string language;            ///< track language.
			std::string title;               ///< track title.

			/* File input. */
			AVFormatContext* format = nullptr;  ///< AV format of input file.
			AVStream* input_stream = nullptr;   ///< AV stream for input file.
			AVPacket* packet = nullptr;         ///< AV packet buffer.
			bool eof = false;                   ///< end of file indicator.

			/* Output stream. */
			AVStream* output_stream = nullptr;  ///< AV output stream.

			/* Live track information.
			 * If live == true, format/input_stream/packet are not used.
			 */
			bool live = false;                        ///< flag for live track.
			bool finished = false;                    ///< end of stream indicator.
			std::deque<LivePacket> queue = {};        ///< packet queue for live frames.
			std::mutex mutex = {};                    ///< mutex for exclusive access to structure components.
			std::condition_variable condition = {};   ///< condition variable signals finished/available frames.

			TrackFile(void) = default;

			/** explicit copy constructor, because mutex and condition variable are not copyable.
			 * @param other reference to template object.
			 */
			TrackFile(TrackFile&& other) noexcept
				: language(std::move(other.language)),
				title(std::move(other.title)),
				format(std::exchange(other.format, nullptr)),
				input_stream(std::exchange(other.input_stream, nullptr)),
				packet(std::exchange(other.packet, nullptr)),
				eof(other.eof),
				output_stream(other.output_stream),
				live(other.live),
				finished(other.finished),
				queue(std::move(other.queue))
			{
			}
		};

		std::vector<TrackFile> m_tracks;        ///< list of configured input tracks.
		AVFormatContext* m_output_context;      ///< output context.
		bool m_mux_started = false;             ///< flag indicating mux start.
		bool m_mux_finished = false;            ///< flag indicating mux end.

		static TrackMuxer* m_instance;          ///< singleton instance pointer.

		/** private constructor.
		 * @param file_name output file name.
		 */
		explicit TrackMuxer(const std::string& file_name);

		/** Get the next packet from a track.
		 * @param track track information.
		 * @param packet buffer for next packet.
		 * @return true if packet is available, false if the track is finished and has no packets left.
		 */
		bool get_live_packet(TrackFile& track, AVPacket* packet);

		/** Wait until every non-finished live track has at least one packet available. */
		void wait_for_live_packets(void);

		/** release temporary memory. */
		void cleanup(void);

	public:
		/** instance getter.
		 * @param file_name optional output file name,
		 * only needed for the first instanciation of this class.
		 * @return reference to the single instance.
		 */
		static TrackMuxer& instance(const std::string& file_name = "");

		/** add an input file to the list of tracks.
		 * @param file_name input file name.
		 * @param track_index track index in input file.
		 * @param format_name AV codec ID of the track in input file.
		 * @param language language of the track.
		 * @param title title for the track in output file.
		 */
		void add_file(
			const std::string& file_name,
			const size_t track_index,
			const std::string& format_name,
			const std::string& language,
			const std::string& title);

		/** Add a live track.
		 * @param info track information from demuxer.
		 * @return output track id used by push() and finish_track().
		 */
		size_t add_live_track(const TrackConverter::TrackInfo& info);

		/** Push one encoded packet into a live track.
		 * The data must represent one complete encoded packet/frame,
		 * e.g. one H.264 access unit, one SUP packet, or one subtitle
		 * packet depending on the codec.
		 * pts/dts/duration use the time_base passed to add_live_track().
		 * @param track_id ID number of the track.
		 * @param data data for one frame.
		 * @param pts presentation timestamp.
		 * @param dts decoding timestamp.
		 * @param duration frame display duration.
		 * @param flags additional flags for the track.
		 */
		void push(
			size_t track_id,
			const std::vector<uint8_t>& data,
			int64_t pts,
			int64_t dts,
			int64_t duration = 0,
			int flags = 0);

		/** Signal that no more packets will be submitted for this track.
		 * @param track_id ID number of track.
		 */
		void finish_track(size_t track_id);

		/** Start muxing.
		 * This function blocks until all tracks have finished.
		 */
		void mux(void);
};

#endif
