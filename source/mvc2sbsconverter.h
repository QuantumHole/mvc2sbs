// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef MVC2SBSCONVERTER_H
#define MVC2SBSCONVERTER_H

#include "edge264.h"

#include <algorithm>
#include <cstdint>
#include <cerrno>
#include <ostream>
#include <stdexcept>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

class MVC2SBSConverter
{
	public:
		typedef enum
		{
			FORMAT_YUV4MPEG2,
			FORMAT_H264
		}
		output_format_t;
		explicit MVC2SBSConverter(std::ostream& output, const output_format_t format);
		~MVC2SBSConverter(void);

		bool feed(const std::vector<uint8_t>& packet);
		bool finish(void);

	private:
		MVC2SBSConverter(const MVC2SBSConverter&);
		MVC2SBSConverter& operator=(const MVC2SBSConverter&);

		// NAL parsing
		bool processInput(const bool endOfStream);
		size_t findStartCode(const size_t from) const;

		// edge264
		bool decodeNAL(const size_t begin, const size_t end);
		bool drainFrames(void);
		bool flush_yuv4mpeg2(void);
		bool flush_h264(void);

		// output
		bool writeFrameYUV4MPEG2(const Edge264Frame& frame);
		bool writeFrameH264(const Edge264Frame& frame);

		Edge264Decoder* decoder_;
		std::ostream& output_;

		std::vector<uint8_t> input_;
		size_t consumed_;

		bool finished_;

		// Y4M
		bool y4mStarted_;

		// Number of corrupt NAL units skipped.
		unsigned skippedCorrupt_;

		const output_format_t m_format;

		// libAV interface
		AVCodecContext* m_context_av = nullptr;
		AVFrame* m_frame_av = nullptr;
		AVPacket* m_packet_av = nullptr;
		size_t m_frame_index;
};

#endif
