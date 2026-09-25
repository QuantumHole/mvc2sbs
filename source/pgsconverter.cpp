#include "pgsconverter.h"

#include <iostream>
#include <fstream>

PGSConverter::PGSConverter(const std::string& input_file_name, const size_t track_number, const std::string& track_id, const std::string& language) :
	TrackConverter(input_file_name, track_number, track_id),
	m_language(language)
{
}

PGSConverter::~PGSConverter(void)
{
}

const std::string PGSConverter::output_extension(void) const
{
	return "." + m_language + ".sup";
}

void PGSConverter::writeUint32BE(const uint32_t value)
{
	const unsigned char buffer[4] =
	{
		static_cast<unsigned char>((value >> 24) & 0xff),
		static_cast<unsigned char>((value >> 16) & 0xff),
		static_cast<unsigned char>((value >> 8) & 0xff),
		static_cast<unsigned char>(value & 0xff)
	};

	write(buffer, sizeof(buffer));
}

size_t PGSConverter::process_data(const std::vector<uint8_t>& data, const uint64_t timestamp)
{
	if (data.empty())
	{
		std::cerr << "Leerer PGS-Frame in language " << m_language << std::endl;
		return 0;
	}

	const size_t size = data.size();
	size_t offset = 0;

	while (offset < size)
	{
		if (size - offset < 3)
		{
			std::cerr << "    Ungültiges PGS-Segment" << std::endl;
			return offset;
		}

		const uint8_t segmentType = data[offset];

		const uint16_t segmentLength = static_cast<uint16_t>(
			(static_cast<uint16_t>(data[offset + 1]) << 8) |
			static_cast<uint16_t>(data[offset + 2]));

		const size_t segmentSize = 3 + static_cast<size_t>(segmentLength);

		if (segmentSize > size - offset)
		{
			std::cerr
			    << "    PGS-Segment außerhalb des Buffers: "
			    << "type=0x"
			    << std::hex
			    << static_cast<unsigned int>(segmentType)
			    << std::dec
			    << ", length="
			    << segmentLength
			    << std::endl;

			return offset;
		}

		const unsigned char magic[2] =
		{
			0x50,
			0x47
		};

		write(magic, sizeof(magic));

		// PTS are sampled with 90kHz
		const uint32_t pts = static_cast<uint32_t>(timestamp * 90000 / 1000000000);
		writeUint32BE(pts);
		writeUint32BE(0);
		write(&data[offset], segmentSize);

		offset += segmentSize;
	}

	return offset;
}
