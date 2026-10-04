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

/** MKVStreamer reads an MKV file and distributes all tracks into
 * TrackConverters configured for the respective type of track.
 */
class MKVStreamer
{
	private:
		const std::string m_input_file_name;                        ///< input file name.
		size_t m_input_file_size;                                   ///< input file size.
		std::map<uint64_t, TrackConverter*> m_converters;           ///< map of configured converters for recognized tracks.
		std::chrono::steady_clock::time_point m_last_status_time;   ///< timing information for status output.

		/** process block of MKV data.
		 * @param block current matroska block.
		 * @param stream EBML input stream.
		 * @param clusterTimecode time code of current cluster.
		 * @param timecodeScale scale factor for all time codes.
		 */
		void readBlock(
			libmatroska::KaxInternalBlock& block,
			libebml::EbmlStream& stream,
			const uint64_t clusterTimecode,
			const uint64_t timecodeScale);

		/** process block group and iterate over blocks.
		 * @param stream EBML input stream.
		 * @param groupEnd end of current block group.
		 * @param clusterTimecode time code of current cluster.
		 * @param timecodeScale scale factor for all time codes.
		 */
		void streamBlockGroup(libebml::EbmlStream& stream,
		                      const uint64_t groupEnd,
		                      const uint64_t clusterTimecode,
		                      const uint64_t timecodeScale);

		/** processes clusters, iterates over blocks.
		 * @param stream EBML stream.
		 * @param clusterEnd end of current cluster.
		 * @param timecodeScale scale parameter for all time codes.
		 */
		void streamCluster(libebml::EbmlStream& stream, const uint64_t clusterEnd, const uint64_t timecodeScale);

		/** reads track list from MKV file.
		* associates tracks to TrackConverters
		* before iterating over Clusters and Blocks.
		* @param tracksElement current matroska track element.
		* @param stream EBML input stream.
		*/
		void readTracks(libmatroska::KaxTracks& tracksElement, libebml::EbmlStream& stream);

		/** process segment of MKV file.
		 * @param stream EBML stream.
		 * @param fileSize size of input file.
		 */
		void streamSegment(libebml::EbmlStream& stream, const size_t fileSize);

	public:

		/** constructor.
		 * @param filename input file name.
		 */
		MKVStreamer(const std::string& filename);

		/** destructor. */
		~MKVStreamer(void);

		/** main function for conversion. */
		void run(void);
};

#endif
