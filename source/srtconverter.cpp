#include "srtconverter.h"
#include "trackmuxer.h"
#include "timetools.h"

#include <iostream>

SRTConverter::SRTConverter(const std::string& input_file_name, const TrackInfo& info) :
    TrackConverter(update_track_info(info)),
    m_output_srt(input_file_name + "." + std::to_string(info.trackNumber) + "." + info.language + ".srt"),
    m_line_index(0)
{
}

TrackConverter::TrackInfo SRTConverter::update_track_info(const TrackInfo& info) const
{
    TrackInfo srtinfo = info;
    srtinfo.codecId = "S_TEXT/UTF8";
    srtinfo.title += " SRT";
    return srtinfo;
}

size_t SRTConverter::process_data(const std::vector<uint8_t>& data, const FrameInfo& info)
{
    if (data.empty())
    {
        std::cerr << "Empty SRT frame in language " << track_info().language << std::endl;
        return 0;
    }

	TrackMuxer& muxer = TrackMuxer::instance();
	const int64_t pts = static_cast<int64_t>(info.pts / track_info().timestampScale);
	const int64_t dts = static_cast<int64_t>(info.dts / track_info().timestampScale);
	const int64_t duration = static_cast<int64_t>(info.duration / track_info().timestampScale);
	muxer.push(
		mux_id(),
		data,
		pts,
		dts,
		duration,
		0);

	m_output_srt << m_line_index << std::endl;
	m_output_srt << timestamp(static_cast<double>(info.pts) / 1e9) << " --> "
	             << timestamp(static_cast<double>(info.pts + info.duration) / 1e9) << std::endl;
	m_output_srt.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
	m_output_srt << std::endl << std::endl;
	m_line_index++;

    return data.size();
}
