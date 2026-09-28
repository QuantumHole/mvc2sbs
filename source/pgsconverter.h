#ifndef PGSCONVERTER_H
#define PGSCONVERTER_H

#include "trackconverter.h"
#include "pgsstream.h"
#include "ocr.h"

/** reads MKV track into a list of PGS subtitle frames. */
class PGSConverter : public TrackConverter
{
	private:
		const bool m_save_images;
		PGSStream m_pgs_stream;
		std::ofstream m_output_srt;
		OCR m_ocr;
		size_t m_line_index;

		void append_ocr(const PGSStream::Frame& frame);

	protected:
		virtual const std::string output_extension(void) const override;

	public:
		PGSConverter(const std::string& input_file_name, const TrackInfo& info);
		virtual ~PGSConverter(void) override;
		virtual size_t process_data(const std::vector<uint8_t>& data, const FrameInfo& info) override;
};

#endif
