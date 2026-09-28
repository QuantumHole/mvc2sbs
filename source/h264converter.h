#ifndef H264CONVERTER_H
#define H264CONVERTER_H

#include "trackconverter.h"

/** reads MKV track into a list of h264 video frames. */
class H264Converter : public TrackConverter
{
	public:
		H264Converter(const std::string& input_file_name, const TrackInfo& info);
		virtual ~H264Converter(void) override;
		virtual size_t process_data(const std::vector<uint8_t>& data, const FrameInfo& info) override;

	protected:
		virtual const std::string output_extension(void) const override;

	private:
};

#endif
