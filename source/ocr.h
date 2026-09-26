#ifndef OCR_H
#define OCR_H

#include <stdint.h>
#include <tesseract/baseapi.h>

class OCR
{
    private:
		tesseract::TessBaseAPI m_ocr;
		bool m_ocr_available;

    public:
        OCR(const std::string& language);
        ~OCR(void);
        std::string read_text(
            const std::vector<uint32_t>& pixels,
            const size_t width,
            const size_t height);
};

#endif
