// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef MKVSTREAMER_H
#define MKVSTREAMER_H

#include "trackconverter.h"

#include <ebml/EbmlStream.h>
#include <matroska/KaxBlock.h>
#include <matroska/KaxTracks.h>
#include <string>
#include <map>
#include <fstream>

class MKVStreamer
{
	private:
		const std::string m_input_file_name;
		size_t m_input_file_size;
		std::map<uint64_t, TrackConverter*> m_converters;
		std::chrono::steady_clock::time_point m_last_status_time;

		void readBlock(
			libmatroska::KaxInternalBlock& block,
			libebml::EbmlStream& stream,
			const uint64_t clusterTimecode,
			const uint64_t timecodeScale);
		void streamBlockGroup(libebml::EbmlStream& stream,
		                      const uint64_t groupEnd,
		                      const uint64_t clusterTimecode,
		                      const uint64_t timecodeScale);
		void streamCluster(libebml::EbmlStream& stream, const uint64_t clusterEnd, const uint64_t timecodeScale);
		void readTracks(libmatroska::KaxTracks& tracksElement, libebml::EbmlStream& stream);
		void streamSegment(libebml::EbmlStream& stream, const size_t fileSize);

	public:
		MKVStreamer(const std::string& filename);
		~MKVStreamer(void);
		void run(void);
};

#endif
