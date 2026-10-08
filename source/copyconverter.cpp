#include "copyconverter.h"

CopyConverter::CopyConverter(const std::string& input_file_name, const TrackInfo& trackinfo) :
	TrackConverter(input_file_name, trackinfo)
{
}

size_t CopyConverter::process_data(const std::vector<uint8_t>& data, const FrameInfo&)
{
	write(data.data(), data.size());
	return data.size();
}
