// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "srtconverter.h"
#include "srtcue.h"
#include "trackmuxer.h"
#include "timetools.h"
#include "spectrum.h"

#include <iostream>

std::vector<SRTConverter::Speaker> SRTConverter::m_speakers;

SRTConverter::SRTConverter(const std::string& input_file_name, const TrackInfo& info, const bool save_file) :
	TrackConverter(update_track_info(info)),
	m_save_file(save_file),
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

std::string SRTConverter::sub2string(const std::string& text, const size_t speaker_index, const bool show_name) const
{
	if ((speaker_index == 0) || (speaker_index >= m_speakers.size()))
	{
		return text;
	}
    std::stringstream s;
    s << "<font color=\"#" << m_speakers[speaker_index].color << "\">"; 
    if (show_name)
    {
        s << m_speakers[speaker_index].name << ": ";
    }
    s << text << "</font> ";

	return s.str();
}

size_t SRTConverter::process_data(const std::vector<uint8_t>& data, const FrameInfo& info)
{
	if (data.empty())
	{
		std::cerr << "Empty SRT frame in language " << track_info().language << std::endl;
		return 0;
	}

	const std::string text(data.begin(), data.end());
	const std::vector<SRTSegment> segments = splitSpeakers(text, info.pts, info.duration);

	TrackMuxer& muxer = TrackMuxer::instance();
	const uint64_t timestampScale = track_info().timestampScale;

	for (const SRTSegment& segment : segments)
	{
        const size_t speaker_index = 0;
        // const size_t speaker_index = m_line_index % m_speakers.size(); // cycle through speakers

		const std::string markedText = sub2string(segment.text, speaker_index, false);

		const std::vector<uint8_t> segmentData(markedText.begin(), markedText.end());
		const int64_t pts = static_cast<int64_t>(segment.startNs / timestampScale);
		const int64_t duration = static_cast<int64_t>(segment.durationNs / timestampScale);
		muxer.push(mux_id(), segmentData, pts, pts, duration, 0);

		if (m_save_file)
		{
			m_output_srt << m_line_index << std::endl;
			m_output_srt << timestamp(static_cast<double>(segment.startNs) / 1e9) << " --> "
			             << timestamp(static_cast<double>(segment.startNs + segment.durationNs) / 1e9)
			             << std::endl;
			m_output_srt.write(
				reinterpret_cast<const char*>(segmentData.data()),
				static_cast<std::streamsize>(segmentData.size()));
			m_output_srt << std::endl << std::endl;
			m_line_index++;
		}
	}

	return data.size();
}

void SRTConverter::define_speakers(const std::vector<std::string>& speakers)
{
	const std::vector<std::string> spectrum = Spectrum().colors(speakers.size());

	m_speakers.resize(speakers.size() + 1); // Resize to accommodate speakers plus an extra entry for index 0 (unknown speaker)
	m_speakers[0] = { "Unknown", "#FFFFFF" }; // Default entry for unknown speaker
	for (size_t i = 0; i < speakers.size(); ++i)
	{
		m_speakers[i + 1] = { speakers[i], spectrum[i] };
	}
}
