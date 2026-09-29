#include "ocr.h"

static std::vector<uint8_t> rgbaToGreyscale(const std::vector<uint32_t>& pixels)
{
	std::vector<uint8_t> greyscale(pixels.size());

	for (size_t i = 0; i < pixels.size(); ++i)
	{
		const uint32_t argb = pixels[i];

		const uint8_t a = static_cast<uint8_t>((argb >> 24) & 0xff);
		uint8_t r       = static_cast<uint8_t>((argb >> 16) & 0xff);
		uint8_t g       = static_cast<uint8_t>((argb >> 8)  & 0xff);
		uint8_t b       = static_cast<uint8_t>(argb        & 0xff);

		// transparency mapped to black
		r = static_cast<uint8_t>(a * r / 255);
		g = static_cast<uint8_t>(a * g / 255);
		b = static_cast<uint8_t>(a * b / 255);

		// Wahrnehmungsgewichtete Umwandlung in Graustufen.
		greyscale[i] = static_cast<uint8_t>((299 * r + 587 * g + 114 * b) / 1000);
	}
	return greyscale;
}

OCR::OCR(const std::string& language) :
	m_ocr(),
	m_ocr_available(false)
{
	if (language.empty())
	{
		return;
	}

	m_ocr_available = (m_ocr.Init(nullptr, language.c_str(), tesseract::OEM_LSTM_ONLY) == 0);

	if (m_ocr_available)
	{
		m_ocr.SetPageSegMode(tesseract::PSM_SINGLE_BLOCK);        // success rate: 1062 / 1062
		// m_ocr.SetPageSegMode(tesseract::PSM_SINGLE_LINE);         // success rate: 1033 / 1062
		// m_ocr.SetPageSegMode(tesseract::PSM_SINGLE_WORD);         // success rate: 1062 / 1062
		// m_ocr.SetPageSegMode(tesseract::PSM_SINGLE_CHAR);         // success rate: 1062 / 1062
		// m_ocr.SetPageSegMode(tesseract::PSM_SPARSE_TEXT);         // success rate: 1061 / 1062
		// m_ocr.SetPageSegMode(tesseract::PSM_SPARSE_TEXT_OSD);     // success rate: 1059 / 1062
		// m_ocr.SetPageSegMode(tesseract::PSM_RAW_LINE);            // success rate: 1062 / 1062
		// m_ocr.SetPageSegMode(tesseract::PSM_COUNT);               // success rate: 1062 / 1062
	}
}

OCR::~OCR(void)
{
	if (m_ocr_available)
	{
		m_ocr.End();
	}
}

std::string OCR::read_text(
	const std::vector<uint32_t>& pixels,
	const size_t width,
	const size_t height)
{
	const size_t pixelCount = width * height;

	if ((width == 0) ||
	    (height == 0) ||
	    (pixels.size() != pixelCount) ||
	    !m_ocr_available)
	{
		return "";
	}

	// Tesseract erhält ein 8-Bit-Graustufenbild.
	std::vector<uint8_t> greyscale = rgbaToGreyscale(pixels);

	if (greyscale.size() == 0)
	{
		return "";
	}

	m_ocr.SetImage(
		greyscale.data(),
		static_cast<int>(width),
		static_cast<int>(height),
		1,                          // Bytes pro Pixel
		static_cast<int>(width));   // Bytes pro Zeile

	// currently leads to memory leak or use-after-free when the first thread exits
	const char* recognizedText = m_ocr.GetUTF8Text();
	// const char* recognizedText = "Test";

	if (!recognizedText)
	{
		return "";
	}

	std::string text;
	text.assign(recognizedText);
	delete[] recognizedText;

	return text;
}
