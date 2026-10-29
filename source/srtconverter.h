#ifndef SRTCONVERTER_H
#define SRTCONVERTER_H

#include "trackconverter.h"
#include <fstream>

/** converter for handling SubRip subtitles. */
class SRTConverter : public TrackConverter
{
	private:
		std::ofstream m_output_srt;   ///< output stream for the translation into SRT format.
		size_t m_line_index;          ///< message counter for the SRT output.

		/** updates the track information for the SRT output.
		 * @param info input track information.
		 * @return updated track information for the SRT output.
		 */
		TrackConverter::TrackInfo update_track_info(const TrackInfo& info) const;

		/** splits the input data into individual speaker segments.
		 * @param data input data to be processed.
		 * @param info frame information for the current packet of data.
		 * @return number of consumed input bytes.
		 */
		size_t split_speakers(const std::vector<uint8_t>& data, const FrameInfo& info);

	public:
		/** constructor.
		 * @param input_file_name path to input file.
		 * @param info information about the assigned track.
		 */
		explicit SRTConverter(const std::string& input_file_name, const TrackInfo& info);

		/** process packets of data.
		 * @param data one bunch of data to be processed. Data may be cut at arbitrary positions into packets.
		 * @param info meta data for the current frame, that may be contained in the current packet of data.
		 * @return number of processed bytes. This must be the size of data, everything else is considered an error.
		 */
		size_t process_data(const std::vector<uint8_t>& data, const FrameInfo& info) override;
};

#endif
