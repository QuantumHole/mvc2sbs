// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef OCR_H
#define OCR_H

#include <stdint.h>
#include <tesseract/baseapi.h>

/** interface for calling tesseract for optical character recognitionn (OCR). */
class OCR
{
	private:
		tesseract::TessBaseAPI m_ocr;    ///< tesseract public API.
		bool m_ocr_available;            ///< flag indicating that tesseract has been initialized.

		/** fix common issues in text recognition.
		 * @param text text to fix.
		 * @return fixed text.
		 */
		std::string fix_recognition_mistakes(const std::string& text) const;

	public:
		/** constructor.
		 * @param language track language, needed for initialization of tesseract.
		 */
		OCR(const std::string& language);

		/** destructor. */
		~OCR(void);

		/** translate image data into text.
		 * @param pixels image data in RGBA format.
		 * @param width image width.
		 * @param height image height.
		 * @return recognized text.
		 */
		std::string read_text(
			const std::vector<uint32_t>& pixels,
			const size_t width,
			const size_t height);
};

#endif
