// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

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
			int flags = 0;                   ///< additional flags for the packet.

			LivePacket(void) :
				data(),
				pts(AV_NOPTS_VALUE),
				dts(AV_NOPTS_VALUE),
				duration(0),
				flags(0)
			{
			}
		};

		/** result of live packet reception. */
		typedef enum
		{
			PacketReady,        ///< live packet available in queue.
			NoPacket,           ///< no live packet available in queue.
			EndOfStream         ///< no more live packets will be submitted for this track.
		}
		LivePacketResult;

		/** state of track configuration. */
		typedef enum
		{
			Ready,              ///< track is ready for configuration.
			Pending,            ///< track is waiting for configuration.
			Configured,         ///< track has been configured.
			Failed              ///< track configuration has failed.
		}
		ConfigurationState;

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
			AVRational input_time_base = AVRational{0, 1}; ///< time base used by live packets.

			/* Live track information.
			 * If live == true, format/input_stream/packet are not used.
			 */
			bool live = false;                        ///< flag for live track.
			bool finished = false;                    ///< end of stream indicator.
			ConfigurationState configuration = ConfigurationState::Ready;  ///< track configuration status.
			std::deque<LivePacket> queue = {};        ///< packet queue for live frames.
			std::mutex mutex = {};                    ///< mutex for exclusive access to structure components.
			std::condition_variable condition = {};   ///< condition variable signals finished/available frames.

			TrackFile(void) :
				language(),
				title(),
				format(nullptr),
				input_stream(nullptr),
				packet(nullptr),
				eof(false),
				output_stream(nullptr),
				input_time_base({0, 1}),
				live(false),
				finished(false),
				configuration(ConfigurationState::Ready),
				queue(),
				mutex(),
				condition()
			{
			}

			TrackFile(const TrackFile&) = delete;
			TrackFile& operator=(const TrackFile&) = delete;

			/** explicit copy constructor, because mutex and condition variable are not copyable.
			 * @param other reference to template object.
			 */
			TrackFile(TrackFile && other) noexcept:
				language(std::move(other.language)),
				title(std::move(other.title)),
				format(std::exchange(other.format, nullptr)),
				input_stream(std::exchange(other.input_stream, nullptr)),
				packet(std::exchange(other.packet, nullptr)),
				eof(other.eof),
				output_stream(other.output_stream),
				input_time_base(other.input_time_base),
				live(other.live),
				finished(other.finished),
				configuration(other.configuration),
				queue(std::move(other.queue))
			{
			}
		};

		std::vector<TrackFile> m_tracks;        ///< list of configured input tracks.
		AVFormatContext* m_output_context;      ///< output context.
		bool m_mux_started = false;             ///< flag indicating mux start.
		bool m_mux_finished = false;            ///< flag indicating mux end.
		std::mutex m_configuration_mutex;       ///< mutex for exclusive access to configuration state.
		std::condition_variable m_configuration_changed;   ///< condition variable signals configuration state change.
		bool m_configuration_failed = false;    ///< flag indicating configuration failure.

		static TrackMuxer* m_instance;          ///< singleton instance pointer.

		/** private constructor.
		 * @param file_name output file name.
		 */
		explicit TrackMuxer(const std::string& file_name);
		TrackMuxer(const TrackMuxer&) = delete;
		TrackMuxer& operator=(const TrackMuxer&) = delete;

		/** Get the next packet from a track.
		 * @param track track information.
		 * @param packet buffer for next packet.
		 * @return status of packet acquisition.
		 */
		LivePacketResult get_live_packet(TrackFile& track, AVPacket* packet);

		/** Wait until every non-finished live track has at least one packet available. */
		void wait_for_live_packets(void);

		/** allocate packets for all tracks and write output file header. */
		void prepare_stream(void);

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

		/** Add a live track that is already configured.
		 * @param info track information from demuxer.
		 * @param configured true if the track is already configured, false if it will be configured later.
		 * @return output track id used by push() and finish_track().
		 */
		size_t add_live_track(const TrackConverter::TrackInfo& info, const bool configured = true);

		/** Publish final codec parameters before the output header is written.
		 * @param track_id ID number of the track.
		 * @param parameters codec parameters for the track.
		 * @param time_base time base used by the live packets.
		 * @throws std::logic_error if the track is not a live track.
		 * @throws std::out_of_range if the track_id is invalid.
		 * @throws std::runtime_error if the track is already configured, finished, or failed.
		 */
		void configure_live_track(const size_t track_id, const AVCodecParameters& parameters, const AVRational time_base);

		/** Mark a track's output configuration as failed and wake mux().
		 * @param track_id ID number of the track.
		 * @throws std::logic_error if the track is not a live track.
		 * @throws std::out_of_range if the track_id is invalid.
		 * @throws std::runtime_error if the track is already configured, finished, or failed.
		 */
		void fail_live_track(const size_t track_id);

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
			const size_t track_id,
			const std::vector<uint8_t>& data,
			const int64_t pts,
			const int64_t dts,
			const int64_t duration = 0,
			const int flags = 0);

		/** Signal that no more packets will be submitted for this track.
		 * @param track_id ID number of track.
		 */
		void finish_track(const size_t track_id);

		/** Start muxing.
		 * This function blocks until all tracks have finished.
		 */
		void mux(void);
};

#endif
