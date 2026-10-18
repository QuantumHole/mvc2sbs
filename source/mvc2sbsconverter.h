// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef MVC2SBSCONVERTER_H
#define MVC2SBSCONVERTER_H

#include "edge264mvc.h"

#include <algorithm>
#include <cstdint>
#include <cerrno>
#include <ostream>
#include <stdexcept>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

/** converts video from MVC format to side-by-side (SBS) format.
 * If the output format is chosen as H.264, the video stream is
 * recompressed using libavcodec from ffmpeg.
 */
class MVC2SBSConverter
{
	public:
		/** constructor.
		 * @param output output stream for converted data.
		 * @param muxer_id ID number of the muxed output stream.
		 */
		explicit MVC2SBSConverter(std::ostream& output, const size_t muxer_id);

		/** destructor. */
		~MVC2SBSConverter(void);

		/** feed a packet of input data into the queue.
		 * @param packet bunch of input data.
		 * @return success status of queuing the packet.
		 */
		bool feed(const std::vector<uint8_t>& packet);

		/** process all remaining data in input queue.
		 * @return success status of processing the data.
		 */
		bool finish(void);

	private:
		/** private copy constructor.
		 * @param obj template object.
		 */
		MVC2SBSConverter(const MVC2SBSConverter& obj);

		/** private assignment operator.
		 * @param obj template object.
		 * @return copied object.
		 */
		MVC2SBSConverter& operator=(const MVC2SBSConverter& obj);

		/** process buffered Annex-B input in input buffer.
		 * Ignores all data before the first start code.
		 * Decode complete NAL, if two start codes have been found.
		 * @param endOfStream flag for the end of stream,
		 * indicating no more data to be received.
		 * @return flag indicating that all data has been processed.
		 */
		bool processInput(const bool endOfStream);

		/** look for Annex-B start-code (00 00 01 or 00 00 00 01)
		 * in input data stream.
		 * @param from start index in input data.
		 * @return index of start code.
		 */
		size_t findStartCode(const size_t from) const;

		/** decode NAL found in input buffer.
		 * @param begin start index of NAL in input buffer.
		 * @param end end index of NAL in input buffer.
		 * @return flag indicating success or failure of decoding.
		 */
		bool decodeNAL(const size_t begin, const size_t end);

		/** receive decoded frames from edge264.
		 * @return success status of processing received frames.
		 */
		bool drainFrames(void);

		/** write complete frame into output stream.
		 * The output format is H.264.
		 * @param edge264frame completely decoded frame.
		 * @return success status of writing frame into output stream.
		 */
		bool writeFrameH264(const Edge264MvcFrame& edge264frame);

		Edge264MvcDecoder* m_decoder;         ///< external decoder for MVC frames
		std::ostream& m_output;            ///< output stream for compressed or uncompressed frames.
		std::vector<uint8_t> m_input;      ///< input buffer for data passed into feed().
		bool m_finished;                   ///< flag that finish() has been called.

		// libAV interface
		AVCodecContext* m_context_av = nullptr;  ///< AVCodec context.
		AVFrame* m_frame_av = nullptr;           ///< AVFrame buffer.
		AVPacket* m_packet_av = nullptr;         ///< AVPacket buffer.
		size_t m_frame_index;                    ///< frame counter for timing information.
		const size_t m_muxer_track_id;           ///< muxer track ID.
};

#endif
