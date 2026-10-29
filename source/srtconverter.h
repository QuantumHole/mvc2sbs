// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SRTCONVERTER_H
#define SRTCONVERTER_H

#include "trackconverter.h"
#include <fstream>

/** converter for handling SubRip subtitles. */
class SRTConverter : public TrackConverter
{
	private:
        /** collection of information about a speaker. */
		struct Speaker
		{
			std::string name = "";  ///< name of the speaker.
			std::string color = "FFFFFF"; ///< color associated with the speaker.
		};

		static std::vector<Speaker> m_speakers;  ///< mapping of speaker indices to speaker properties.

		const bool m_save_file;             ///< flag to indicate whether the SRT output file should be saved.
		std::ofstream m_output_srt;         ///< output stream for the translation into SRT format.
		size_t m_line_index;                ///< message counter for the SRT output.

		/** updates the track information for the SRT output.
		 * @param info input track information.
		 * @return updated track information for the SRT output.
		 */
		TrackConverter::TrackInfo update_track_info(const TrackInfo& info) const;

		/** marks the speaker in the subtitle text.
		 * @param text input subtitle text.
		 * @param speaker_index index of the speaker to mark.
		 * @param show_name flag to indicate whether to show the speaker's name.
		 * @return modified subtitle text with speaker marker.
		 */
		std::string sub2string(const std::string& text, const size_t speaker_index, const bool show_name) const;

	public:
		/** constructor.
		 * @param input_file_name path to input file.
		 * @param info information about the assigned track.
		 * @param save_file flag to indicate whether the SRT output file should be saved.
		 */
		explicit SRTConverter(const std::string& input_file_name, const TrackInfo& info, const bool save_file = false);

		/** process packets of data.
		 * @param data one bunch of data to be processed. Data may be cut at arbitrary positions into packets.
		 * @param info meta data for the current frame, that may be contained in the current packet of data.
		 * @return number of processed bytes. This must be the size of data, everything else is considered an error.
		 */
		size_t process_data(const std::vector<uint8_t>& data, const FrameInfo& info) override;

		/** defines the speakers for the SRT output.
		 * @param speakers list of speaker names.
		 */
		static void define_speakers(const std::vector<std::string>& speakers);
};

#endif
