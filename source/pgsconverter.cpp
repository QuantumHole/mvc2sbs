#include "pgsconverter.h"

PGSConverter::PGSConverter(const std::string& language, const std::string& track_id, const std::string& input_file_name) :
    TrackConverter(input_file_name, track_id),
    m_language(language)
{
}

PGSConverter::~PGSConverter(void)
{
}

void PGSConverter::process_data(void)
{
}
