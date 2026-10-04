// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PGSCONVERTER_H
#define PGSCONVERTER_H

#include "trackconverter.h"
#include "pgsstream.h"
#include "ocr.h"

/** reads MKV track into a list of PGS subtitle frames. */
class PGSConverter : public TrackConverter
{
	private:
		const bool m_save_images;     ///< flag for storing all subtitles as PNG files.
		PGSStream m_pgs_stream;       ///< handler for PGS specific decoding/encoding/editing
		std::ofstream m_output_srt;   ///< output stream for the translation into SRT format.
		OCR m_ocr;                    ///< interface to tesseract API.
		size_t m_line_index;          ///< message counter for the SRT output.

		/** perform OCR, store images, write SRT as configured.
		 * @param frame input frame for all configured actions.
		 */
		void append_ocr(const PGSStream::PGSFrame& frame);

	protected:
		/** file extension for the generated output file.
		 * @return file extension for the generated output file.
		 */
		virtual const std::string output_extension(void) const override;

	public:
		/** constructor.
		 * @param input_file_name path to input file.
		 * @param trackinfo information about the assigned track.
		 */
		PGSConverter(const std::string& input_file_name, const TrackInfo& trackinfo);

		/** destructor. */
		virtual ~PGSConverter(void) override;

		/** process packets of data.
		 * @param data one bunch of data to be processed. Data may be cut at arbitrary positions into packets.
		 * @param info meta data for the current frame, that may be contained in the current packet of data.
		 * @return number of processed bytes. This must be the size of data, everything else is considered an error.
		 */
		virtual size_t process_data(const std::vector<uint8_t>& data, const FrameInfo& info) override;
};

#endif
