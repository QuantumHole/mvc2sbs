#ifndef PGSCONVERTER_H
#define PGSCONVERTER_H

#include "trackconverter.h"

class PGSConverter : public TrackConverter
{
	private:
		const std::string m_language;

	public:
		PGSConverter(const std::string& language, const std::string& track_id, const std::string& input_file_name);
		virtual ~PGSConverter(void);
        virtual void process_data(void);
};


#endif
