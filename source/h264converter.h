#ifndef H264CONVERTER_H
#define H264CONVERTER_H

#include "trackconverter.h"
#include "mvc2sbsconverter.h"

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
		size_t m_nal_length_size;
		bool m_codec_private_written;
		std::ofstream m_sbs_output;
		MVC2SBSConverter m_mvc2sbs;

		void write_start_code(std::vector<uint8_t>& data);
		bool write_codec_private(const std::vector<uint8_t>& codecPrivate, std::vector<uint8_t>& data);
		bool write_mvc_codec_private(const std::vector<uint8_t>& codecPrivate, std::vector<uint8_t>& data);
};

#endif
