#ifndef PGSCONVERTER_H
#define PGSCONVERTER_H

#include "trackconverter.h"
#include "pgsstream.h"
#include "ocr.h"

class PGSConverter : public TrackConverter
{
	private:
		const bool m_save_images;
		const std::string m_language;
		PGSStream m_pgs_stream;
		std::ofstream m_output_srt;
		OCR m_ocr;
		size_t m_line_index;

		void append_ocr(const PGSStream::Frame& frame);

	protected:
		virtual const std::string output_extension(void) const override;

	public:
		PGSConverter(const std::string& input_file_name, const size_t track_number, const std::string& track_id, const std::string& language);
		virtual ~PGSConverter(void) override;
		virtual size_t process_data(const std::vector<uint8_t>& data, const uint64_t timestamp) override;
};

#endif
