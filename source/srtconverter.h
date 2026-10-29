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

        TrackConverter::TrackInfo update_track_info(const TrackInfo& info) const;

    public:
        /** constructor. */
        explicit SRTConverter(const std::string& input_file_name, const TrackInfo& info);

		/** process packets of data.
		 * @param data one bunch of data to be processed. Data may be cut at arbitrary positions into packets.
		 * @param info meta data for the current frame, that may be contained in the current packet of data.
		 * @return number of processed bytes. This must be the size of data, everything else is considered an error.
		 */
        size_t process_data(const std::vector<uint8_t>& data, const FrameInfo& info) override;
};

#endif
