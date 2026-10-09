#include "trackmuxer.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>
#include <string>
#include <unordered_map>
// #include <string.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavcodec/codec_id.h>
}

#include "trackconverter.h"


// #error incomplete implementation

TrackMuxer* TrackMuxer::m_instance = nullptr;

/** Convert an FFmpeg error code to a readable string. */
static std::string ffmpeg_error(int error)
{
	char buffer[AV_ERROR_MAX_STRING_SIZE];

	av_strerror(error, buffer, sizeof(buffer));
	return std::string(buffer);
}

/** Throw an exception if an FFmpeg call failed. */
static void check_ffmpeg(const int error, const char* operation)
{
	if (error < 0)
	{
		throw std::runtime_error(
				  std::string(operation) +
				  ": " +
				  ffmpeg_error(error));
	}
}

static AVCodecID matroskaCodecIdToAvCodecId(const std::string& id)
{
    static const std::unordered_map<std::string, AVCodecID> mapping = {
        // Video
        {"V_AV1",             AV_CODEC_ID_AV1},
        {"V_DIRAC",           AV_CODEC_ID_DIRAC},
        {"V_FFV1",            AV_CODEC_ID_FFV1},
        {"V_MJPEG",           AV_CODEC_ID_MJPEG},
        {"V_MPEG1",           AV_CODEC_ID_MPEG1VIDEO},
        {"V_MPEG2",           AV_CODEC_ID_MPEG2VIDEO},
        {"V_MPEG4/ISO/ASP",   AV_CODEC_ID_MPEG4},
        {"V_MPEG4/ISO/AP",    AV_CODEC_ID_MPEG4},
        {"V_MPEG4/ISO/SP",    AV_CODEC_ID_MPEG4},
        {"V_MPEG4/ISO/AVC",   AV_CODEC_ID_H264},
        {"V_MPEG4/MS/V3",     AV_CODEC_ID_MSMPEG4V3},
        {"V_MPEGH/ISO/HEVC",  AV_CODEC_ID_HEVC},
        {"V_PRORES",          AV_CODEC_ID_PRORES},
        {"V_THEORA",          AV_CODEC_ID_THEORA},
        {"V_UNCOMPRESSED",    AV_CODEC_ID_RAWVIDEO},
        {"V_VP8",             AV_CODEC_ID_VP8},
        {"V_VP9",             AV_CODEC_ID_VP9},

        // Audio
        {"A_AAC",             AV_CODEC_ID_AAC},
        {"A_AC3",             AV_CODEC_ID_AC3},
        {"A_ALAC",            AV_CODEC_ID_ALAC},
        {"A_DTS",             AV_CODEC_ID_DTS},
        {"A_EAC3",            AV_CODEC_ID_EAC3},
        {"A_FLAC",            AV_CODEC_ID_FLAC},
        {"A_MLP",             AV_CODEC_ID_MLP},
        {"A_MPEG/L1",         AV_CODEC_ID_MP1},
        {"A_MPEG/L2",         AV_CODEC_ID_MP2},
        {"A_MPEG/L3",         AV_CODEC_ID_MP3},
        {"A_OPUS",            AV_CODEC_ID_OPUS},
        {"A_TRUEHD",          AV_CODEC_ID_TRUEHD},
        {"A_TTA1",            AV_CODEC_ID_TTA},
        {"A_VORBIS",          AV_CODEC_ID_VORBIS},
        {"A_WAVPACK4",        AV_CODEC_ID_WAVPACK},

        // Untertitel
        {"S_TEXT/ASCII",      AV_CODEC_ID_TEXT},
        {"S_TEXT/UTF8",       AV_CODEC_ID_SUBRIP},
        {"S_TEXT/ASS",        AV_CODEC_ID_ASS},
        {"S_TEXT/SSA",        AV_CODEC_ID_ASS},
        {"S_ASS",             AV_CODEC_ID_ASS},
        {"S_SSA",             AV_CODEC_ID_ASS},
        {"S_VOBSUB",          AV_CODEC_ID_DVD_SUBTITLE},
        {"S_DVBSUB",          AV_CODEC_ID_DVB_SUBTITLE},
        {"S_HDMV/PGS",        AV_CODEC_ID_HDMV_PGS_SUBTITLE},
        {"D_WEBVTT/SUBTITLES",    AV_CODEC_ID_WEBVTT},
        {"D_WEBVTT/CAPTIONS",     AV_CODEC_ID_WEBVTT},
        {"D_WEBVTT/DESCRIPTIONS", AV_CODEC_ID_WEBVTT},
        {"D_WEBVTT/METADATA",     AV_CODEC_ID_WEBVTT},
    };

    const auto it = mapping.find(id);
    return it != mapping.end() ? it->second : AV_CODEC_ID_NONE;
}

static AVMediaType matroskaCodecIdToAvMediaType(const std::string& id)
{
    if (id.compare(0, 2, "V_") == 0)
        return AVMEDIA_TYPE_VIDEO;

    if (id.compare(0, 2, "A_") == 0)
        return AVMEDIA_TYPE_AUDIO;

    if (id.compare(0, 2, "S_") == 0 || id.compare(0, 9, "D_WEBVTT/") == 0)
        return AVMEDIA_TYPE_SUBTITLE;

    return AVMEDIA_TYPE_UNKNOWN;
}

static int fillCodecParameters(const TrackConverter::TrackInfo& track, AVCodecParameters* par)
{
    if (!par)
        return AVERROR(EINVAL);

    par->codec_type = matroskaCodecIdToAvMediaType(track.codecId);
    par->codec_id   = matroskaCodecIdToAvCodecId(track.codecId);

    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (track.width > INT_MAX || track.height > INT_MAX)
            return AVERROR(EINVAL);

        par->width  = static_cast<int>(track.width);
        par->height = static_cast<int>(track.height);
    }
	// else if (par->codec_type == AVMEDIA_TYPE_AUDIO)
	// {
    //     if (track.sampleRate <= 0 || track.channels <= 0)
    //         return AVERROR(EINVAL);

    //     par->sample_rate = track.sampleRate;
    //     av_channel_layout_default(&par->ch_layout, track.channels);

    //     // Matroska CodecDelay ist eine Zeit; initial_padding ist in Samples.
    //     if (track.codecDelay > 0) {
    //         if (track.codecDelay > static_cast<uint64_t>(INT64_MAX))
    //             return AVERROR(EINVAL);

    //         const int64_t samples = av_rescale_rnd(
    //             static_cast<int64_t>(track.codecDelay),
    //             track.sampleRate,
    //             1'000'000'000LL,
    //             AV_ROUND_NEAR_INF);

    //         if (samples < 0 || samples > INT_MAX)
    //             return AVERROR(EINVAL);

    //         par->initial_padding = static_cast<int>(samples);
    //     }
    // }

    if (!track.codecPrivate.empty()) {
        if (track.codecPrivate.size() >
            static_cast<size_t>(INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)) {
            return AVERROR(EINVAL);
        }

        par->extradata = static_cast<uint8_t*>(
            av_mallocz(track.codecPrivate.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!par->extradata)
            return AVERROR(ENOMEM);

        memcpy(par->extradata,
                    track.codecPrivate.data(),
                    track.codecPrivate.size());
        par->extradata_size = static_cast<int>(track.codecPrivate.size());
    }

    return 0;
}

static int makeTimeBase(const uint64_t timestampScale, AVRational& timeBase)
{
    if (timestampScale == 0 ||
        timestampScale > static_cast<uint64_t>(INT64_MAX)) {
        return AVERROR(EINVAL);
    }

    // Ein Matroska-Tick entspricht timestampScale / 1e9 Sekunden.
    av_reduce(&timeBase.num, &timeBase.den,
              static_cast<int64_t>(timestampScale),
              1'000'000'000LL, INT_MAX);

    return timeBase.num > 0 && timeBase.den > 0
        ? 0
        : AVERROR(EINVAL);
}

TrackMuxer::TrackMuxer(const std::string& file_name)
{
	/* Create Matroska output. */
	check_ffmpeg(
		avformat_alloc_output_context2(&m_output_context, nullptr, "matroska", file_name.c_str()),
		"avformat_alloc_output_context2");

	if (m_output_context == nullptr)
	{
		throw std::runtime_error("Could not create Matroska output context");
	}

	/* Open output file. */
	if (!(m_output_context->oformat->flags & AVFMT_NOFILE))
	{
		check_ffmpeg(
			avio_open(&m_output_context->pb, file_name.c_str(), AVIO_FLAG_WRITE),
			"avio_open");
	}
}

TrackMuxer& TrackMuxer::instance(const std::string& file_name)
{
	if (m_instance == nullptr)
	{
		m_instance = new TrackMuxer(file_name);
	}
	return *m_instance;
}

void TrackMuxer::add_file(
	const std::string& file_name,
	const size_t track_index,
	const std::string& format_name,
	const std::string& language,
	const std::string& title)
{
	m_tracks.emplace_back();
	TrackFile& track = m_tracks.back();
	track.language = language;
	track.title = title;

	/* open file. */
	const AVInputFormat* input_format = av_find_input_format(format_name.c_str());

	if (input_format == nullptr)
	{
		throw std::runtime_error("Could not find FFmpeg input format: " + format_name);
	}

	/* Open elementary stream. */
	check_ffmpeg(
		avformat_open_input(&track.format, file_name.c_str(), input_format, nullptr),
		"avformat_open_input");

	/* Let FFmpeg determine stream parameters. */
	check_ffmpeg(
		avformat_find_stream_info(track.format, nullptr),
		"avformat_find_stream_info");

	const bool is_container = format_name == "matroska" || format_name == "webm";

	/*
	 * Find the requested stream.
	 *
	 * For our elementary-stream inputs there should normally
	 * be exactly one stream, but don't rely on that.
	 */
	if (is_container)
	{
		if (track_index >= track.format->nb_streams)
		{
			throw std::runtime_error("Track index " + std::to_string(track_index) + " is out of range for: " + file_name);
		}

		track.input_stream = track.format->streams[track_index];
	}
	else
	{
		for (unsigned int stream_index = 0; stream_index < track.format->nb_streams; stream_index++)
		{
			AVStream* stream = track.format->streams[stream_index];

			if ((stream == nullptr) || (stream->codecpar == nullptr))
			{
				continue;
			}

			/*
			 * Ignore data streams. We are interested in actual
			 * audio/video/subtitle streams.
			 */
			if ((stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) ||
			    (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) ||
			    (stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE))
			{
				track.input_stream = stream;
				break;
			}
		}
	}

	if (track.input_stream == nullptr)
	{
		throw std::runtime_error("Could not find requested stream in: " + file_name);
	}

	track.output_stream = avformat_new_stream(m_output_context, nullptr);

	if (track.output_stream == nullptr)
	{
		throw std::runtime_error("avformat_new_stream failed");
	}

	/* Copy codec parameters.
	 *
	 * This copies codec_id, width, height, extradata,
	 * audio parameters, etc.
	 */
	check_ffmpeg(
		avcodec_parameters_copy(track.output_stream->codecpar, track.input_stream->codecpar),
		"avcodec_parameters_copy");

	/*
	 * Use the same time base initially.
	 *
	 * av_packet_rescale_ts() below will convert packet
	 * timestamps from this input time base to this output
	 * time base.
	 */
	track.output_stream->time_base = track.input_stream->time_base;

	/* Track metadata. */
	if (!track.language.empty())
	{
		av_dict_set(&track.output_stream->metadata, "language", track.language.c_str(), 0);
	}

	if (!track.title.empty())
	{
		av_dict_set(&track.output_stream->metadata, "title", track.title.c_str(), 0);
	}
}

size_t TrackMuxer::add_live_track(const TrackConverter::TrackInfo& info)
{
	if (m_mux_started)
	{
		throw std::logic_error("Cannot add a track after muxing has started");
	}

	TrackFile track;

	track.language = info.language;
	track.title = info.title;
	track.live = true;
	track.finished = false;

	track.output_stream = avformat_new_stream(m_output_context, nullptr);

	if (track.output_stream == nullptr)
	{
		throw std::runtime_error("avformat_new_stream failed");
	}

	// fill codec parameters
	check_ffmpeg(
		fillCodecParameters(info, track.output_stream->codecpar),
		"avcodec_parameters_copy");

	makeTimeBase(info.timestampScale, track.output_stream->time_base);

	if (!info.language.empty())
	{
		av_dict_set(
			&track.output_stream->metadata,
			"language",
			info.language.c_str(),
			0);
	}

	if (!info.title.empty())
	{
		av_dict_set(
			&track.output_stream->metadata,
			"title",
			info.title.c_str(),
			0);
	}

	m_tracks.emplace_back(std::move(track));

	return m_tracks.size() - 1;
}

void TrackMuxer::push(
	const size_t track_id,
	const std::vector<uint8_t>& data,
	const int64_t pts,
	const int64_t dts,
	const int64_t duration,
	const int flags)
{
	if (track_id >= m_tracks.size())
	{
		throw std::out_of_range("TrackMuxer::push: invalid track id");
	}

	TrackFile& track = m_tracks[track_id];

	if (!track.live)
	{
		throw std::logic_error("TrackMuxer::push: track is not a live track");
	}

	{
		std::lock_guard<std::mutex> lock(track.mutex);

		if (track.finished)
		{
			throw std::logic_error("TrackMuxer::push: track has already been finished");
		}

		LivePacket packet;

		packet.data = data;
		packet.pts = pts;
		packet.dts = dts;
		packet.duration = duration;
		packet.flags = flags;

		track.queue.emplace_back(std::move(packet));
	}

	track.condition.notify_one();
}

bool TrackMuxer::get_live_packet(TrackFile& track, AVPacket* packet)
{
	std::unique_lock<std::mutex> lock(track.mutex);

	track.condition.wait(lock, [&track]()
		{
			return !track.queue.empty() || track.finished;
		});

	if (track.queue.empty())
	{
		return false;
	}

	LivePacket live_packet = std::move(track.queue.front());
	track.queue.pop_front();
	lock.unlock();
	av_packet_unref(packet);

	if (live_packet.data.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
	{
		throw std::runtime_error("Live packet is too large");
	}

	check_ffmpeg(
		av_new_packet(packet, static_cast<int>(live_packet.data.size())),
		"av_new_packet");

	std::copy(live_packet.data.begin(), live_packet.data.end(), packet->data);

	packet->pts = live_packet.pts;
	packet->dts = live_packet.dts;
	packet->duration = live_packet.duration;
	packet->flags = live_packet.flags;

	return true;
}

void TrackMuxer::mux(void)
{
	if (m_tracks.empty())
	{
		return;
	}

	if (m_mux_started)
	{
		throw std::logic_error("TrackMuxer::mux() called more than once");
	}

	m_mux_started = true;

	try
	{
		/* Allocate packet buffers for all tracks. */
		for (TrackFile& track : m_tracks)
		{
			track.packet = av_packet_alloc();

			if (track.packet == nullptr)
			{
				throw std::runtime_error("av_packet_alloc failed");
			}
		}

		/*
		 * Write Matroska header.
		 *
		 * All tracks must already have been added at this point.
		 */
		check_ffmpeg(
			avformat_write_header(m_output_context, nullptr),
			"avformat_write_header");

		while (true)
		{
			/* Fill one packet for every track. */
			for (TrackFile& track : m_tracks)
			{
				if (track.eof)
				{
					continue;
				}

				if (track.packet->data != nullptr)
				{
					continue;
				}

				if (track.live)
				{
					const bool got_packet = get_live_packet(track, track.packet);

					if (!got_packet)
					{
						track.eof = true;
					}
				}
				else
				{
					while (true)
					{
						const int result = av_read_frame(track.format, track.packet);

						if (result == AVERROR_EOF)
						{
							track.eof = true;
							break;
						}

						check_ffmpeg(result, "av_read_frame");

						if (track.packet->stream_index == track.input_stream->index)
						{
							break;
						}

						av_packet_unref(track.packet);
					}
				}
			}

			/*
			 * Select packet with earliest timestamp.
			 */
			int selected = -1;

			for (size_t i = 0; i < m_tracks.size(); i++)
			{
				const TrackFile& track = m_tracks[i];

				if (track.eof || track.packet->data == nullptr)
				{
					continue;
				}

				if (selected < 0)
				{
					selected = static_cast<int>(i);
					continue;
				}

				const TrackFile& current = m_tracks[static_cast<size_t>(selected)];
				const AVPacket* candidate = track.packet;
				const AVPacket* current_packet = current.packet;
				const int64_t candidate_ts = ((candidate->dts != AV_NOPTS_VALUE) ? candidate->dts : candidate->pts);
				const int64_t current_ts = ((current_packet->dts != AV_NOPTS_VALUE) ? current_packet->dts : current_packet->pts);

				if (candidate_ts == AV_NOPTS_VALUE)
				{
					continue;
				}

				if (current_ts == AV_NOPTS_VALUE)
				{
					selected = static_cast<int>(i);
					continue;
				}

				const AVRational candidate_tb = track.live ? track.output_stream->time_base : track.input_stream->time_base;
				const AVRational current_tb = current.live ? current.output_stream->time_base : current.input_stream->time_base;

				if (av_compare_ts(candidate_ts, candidate_tb, current_ts, current_tb) < 0)
				{
					selected = static_cast<int>(i);
				}
			}

			/*
			 * No packets left.
			 */
			if (selected < 0)
			{
				break;
			}

			TrackFile& track = m_tracks[static_cast<size_t>(selected)];
			AVPacket* packet = track.packet;

			/*
			 * Convert timestamps for file tracks.
			 *
			 * Live packets already use the output stream's
			 * time base.
			 */
			if (!track.live)
			{
				av_packet_rescale_ts(packet, track.input_stream->time_base, track.output_stream->time_base);
			}

			packet->stream_index = track.output_stream->index;

			check_ffmpeg(
				av_interleaved_write_frame(m_output_context, packet),
				"av_interleaved_write_frame");

			/*
			 * av_interleaved_write_frame() consumes the packet.
			 */
			av_packet_unref(packet);
		}

		check_ffmpeg(av_write_trailer(m_output_context), "av_write_trailer");
		cleanup();
		m_mux_finished = true;
	}
	catch (...)
	{
		cleanup();
		throw;
	}
}

void TrackMuxer::finish_track(const size_t track_id)
{
	if (track_id >= m_tracks.size())
	{
		throw std::out_of_range("TrackMuxer::finish_track: invalid track id");
	}

	TrackFile& track = m_tracks[track_id];

	{
		std::lock_guard<std::mutex> lock(track.mutex);

		track.finished = true;
	}

	track.condition.notify_all();
}

void TrackMuxer::cleanup(void)
{
	/* Cleanup input packets. */
	for (TrackFile& input : m_tracks)
	{
		av_packet_free(&input.packet);
		avformat_close_input(&input.format);
	}

	/* Cleanup output. */
	if ((m_output_context != nullptr) &&
		(m_output_context->pb != nullptr) &&
		!(m_output_context->oformat->flags & AVFMT_NOFILE))
	{
		avio_closep(&m_output_context->pb);
	}

	avformat_free_context(m_output_context);
	m_output_context = nullptr;
}
