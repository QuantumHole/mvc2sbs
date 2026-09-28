#ifndef H264CONVERTER_H
#define H264CONVERTER_H

#include "trackconverter.h"

/** reads MKV track into a list of h264 video frames. */
class H264Converter : public TrackConverter
{
    private:
        const std::string m_language;
	protected:
		virtual const std::string output_extension(void) const override;
    public:
		H264Converter(const std::string& input_file_name, const size_t track_number, const std::string& track_id, const std::string& language);
		virtual ~H264Converter(void) override;
		virtual size_t process_data(const std::vector<uint8_t>& data, const uint64_t timestamp) override;
};

#endif
