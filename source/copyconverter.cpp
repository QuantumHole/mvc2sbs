#include "copyconverter.h"
#include "trackmuxer.h"

CopyConverter::CopyConverter(const TrackInfo& trackinfo) :
	TrackConverter(trackinfo)
{
}

size_t CopyConverter::process_data(const std::vector<uint8_t>& data, const FrameInfo& frameinfo)
{
    TrackMuxer& muxer = TrackMuxer::instance();

     // Keyframe-Flag setzen
    const int flags = frameinfo.keyframe ? AV_PKT_FLAG_KEY : 0;

    muxer.push(
        mux_id(),
        data,
        static_cast<int64_t>(frameinfo.pts),
        static_cast<int64_t>(frameinfo.dts),
        static_cast<int64_t>(frameinfo.duration),
        flags);

    return data.size();
}
