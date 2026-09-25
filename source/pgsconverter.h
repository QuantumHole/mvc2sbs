#ifndef PGSCONVERTER_H
#define PGSCONVERTER_H

#include "trackconverter.h"

class PGSConverter : public TrackConverter
{
	private:
		const std::string m_language;

		void writeUint32BE(const uint32_t value);

	protected:
		virtual const std::string output_extension(void) const;

	public:
		PGSConverter(const std::string& input_file_name, const size_t track_number, const std::string& track_id, const std::string& language);
		virtual ~PGSConverter(void);
		virtual size_t process_data(const std::vector<uint8_t>& data, const uint64_t timestamp);
};

#endif
