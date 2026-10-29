// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "copyconverter.h"
#include "trackmuxer.h"

CopyConverter::CopyConverter(const TrackInfo& trackinfo) :
	TrackConverter(trackinfo)
{
}

size_t CopyConverter::process_data(const std::vector<uint8_t>& data, const FrameInfo& frameinfo)
{
	TrackMuxer& muxer = TrackMuxer::instance();
	const int64_t timestampScale = static_cast<int64_t>(track_info().timestampScale);

	if (timestampScale <= 0)
	{
		throw std::runtime_error("Invalid Matroska timestamp scale");
	}

	// Keyframe-Flag setzen
	const int flags = frameinfo.keyframe ? AV_PKT_FLAG_KEY : 0;
	const int64_t pts = static_cast<int64_t>(frameinfo.pts) / timestampScale;
	const int64_t duration = static_cast<int64_t>(frameinfo.duration) / timestampScale;
	const int64_t dts = track_info().codecId.compare(0, 2, "V_") == 0
	    ? AV_NOPTS_VALUE
	    : pts;

	muxer.push(
		mux_id(),
		data,
		pts,
		dts,
		duration,
		flags);

	return data.size();
}
