#include "h264converter.h"

H264Converter::H264Converter(
    const std::string& input_file_name,
    const size_t track_number,
    const std::string& track_id,
    const std::string& language) :
    TrackConverter(input_file_name, track_number, track_id),
    m_language(language)
{
    
}

H264Converter::~H264Converter(void)
{

}

const std::string H264Converter::output_extension(void) const
{
	return "." + m_language + ".264";
}

size_t H264Converter::process_data(const std::vector<uint8_t>& data, const uint64_t timestamp)
{
    (void)timestamp;
    return data.size();
}
