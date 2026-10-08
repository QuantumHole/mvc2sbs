// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef H264CONVERTER_H
#define H264CONVERTER_H

#include "trackconverter.h"
#include "mvc2sbsconverter.h"

/** reads MKV track into a list of h264 video frames. */
class H264Converter : public TrackConverter
{
	public:
		/** constructor.
		 * @param input_file_name path to input file.
		 * @param trackinfo information about the assigned track.
		 */
		H264Converter(const std::string& input_file_name, const TrackInfo& trackinfo);

		/** destructor. */
		virtual ~H264Converter(void) override;

		/** process packets of data.
		 * @param data one bunch of data to be processed. Data may be cut at arbitrary positions into packets.
		 * @param info meta data for the current frame, that may be contained in the current packet of data.
		 * @return number of processed bytes. This must be the size of data, everything else is considered an error.
		 */
		virtual size_t process_data(const std::vector<uint8_t>& data, const FrameInfo& info) override;

	private:
		size_t m_nal_length_size;      ///< size of NAL length, may be 1, 2, or 4.
		bool m_codec_private_written;  ///< flag indicating that codec private data has been written at least once.
		MVC2SBSConverter m_mvc2sbs;    ///< converter for video frames from MVC to SBS.

		/** write Annex-B start code 00 00 00 01 into vector.
		 * @param data output vector.
		 */
		void write_start_code(std::vector<uint8_t>& data);

		/** write codec private data of base view.
		 * @param codecPrivate original codec private data read from input stream.
		 * @param data output vector.
		 * @return success status of write operation.
		 */
		bool write_codec_private(const std::vector<uint8_t>& codecPrivate, std::vector<uint8_t>& data);

		/** write codec private data of MVC view.
		 * @param codecPrivate original codec private data read from input stream.
		 * @param data output vector.
		 * @return success status of write operation.
		 */
		bool write_mvc_codec_private(const std::vector<uint8_t>& codecPrivate, std::vector<uint8_t>& data);
};

#endif
