#include "trackmuxer.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
}

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
    TrackFile track = {language, title};

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
            if (stream == nullptr || stream->codecpar == nullptr)
            {
                continue;
            }

            /*
             * Ignore data streams. We are interested in actual
             * audio/video/subtitle streams.
             */
            if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO ||
                stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
                stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
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

    /*
     * Allocate one packet which remains buffered while
     * choosing the next packet to mux.
     */
    track.packet = av_packet_alloc();

    if (track.packet == nullptr)
    {
        throw std::runtime_error("av_packet_alloc failed");
    }

    m_tracks.emplace_back(track);
}

void TrackMuxer::mux(void)
{
    if (m_tracks.empty())
    {
        throw std::runtime_error("TrackMuxer: no input tracks configured");
    }

    try
    {
        /* Write Matroska header. */
        check_ffmpeg(
            avformat_write_header(m_output_context, nullptr),
            "avformat_write_header");

        /* Read and interleave packets.
         *
         * One packet from every input is kept buffered.
         * We always write the packet with the earliest timestamp.
         */
        while (true)
        {
            /* Fill an empty packet buffer for every input. */
            for (TrackFile& input : m_tracks)
            {
                if (input.eof || (input.packet->data != nullptr))
                {
                    continue;
                }

                while (true)
                {
                    const int result = av_read_frame(input.format, input.packet);

                    if (result == AVERROR_EOF)
                    {
                        input.eof = true;
                        break;
                    }

                    check_ffmpeg(result, "av_read_frame");

                    /*
                     * We expect one stream, but ignore packets
                     * belonging to another stream just in case.
                     */
                    if (input.packet->stream_index == input.input_stream->index)
                    {
                        break;
                    }

                    av_packet_unref(input.packet);
                }
            }

            /* Find the buffered packet with the earliest timestamp. */
            int selected = -1;

            for (size_t i = 0; i < m_tracks.size(); i++)
            {
                const TrackFile& input = m_tracks[i];

                if (input.eof || input.packet->data == nullptr)
                {
                    continue;
                }

                if (selected < 0)
                {
                    selected = static_cast<int>(i);
                    continue;
                }

                const AVPacket* candidate = input.packet;
                const AVPacket* current = m_tracks[static_cast<size_t>(selected)].packet;

                /*
                 * Prefer DTS for interleaving because DTS describes
                 * decoding order. If DTS isn't available, use PTS.
                 */
                const int64_t candidate_ts = ((candidate->dts != AV_NOPTS_VALUE) ? candidate->dts : candidate->pts);
                const int64_t current_ts = ((current->dts != AV_NOPTS_VALUE) ? current->dts : current->pts);

                /* If neither packet has a timestamp, retain the existing order. */
                if (candidate_ts == AV_NOPTS_VALUE)
                {
                    continue;
                }

                if (current_ts == AV_NOPTS_VALUE)
                {
                    selected = static_cast<int>(i);
                    continue;
                }

                /* Compare timestamps using their respective time bases. */
                if (av_compare_ts(
                        candidate_ts,
                        input.input_stream->time_base,
                        current_ts,
                        m_tracks[static_cast<size_t>(selected)].input_stream->time_base) < 0)
                {
                    selected = static_cast<int>(i);
                }
            }

            /* No packet left in any input. */
            if (selected < 0)
            {
                break;
            }

            TrackFile& input = m_tracks[static_cast<size_t>(selected)];
            AVPacket* packet = input.packet;

            /*
             * Convert timestamps from the input time base
             * to the output stream time base.
             */
            av_packet_rescale_ts(packet, input.input_stream->time_base, input.output_stream->time_base);

            /*
             * The packet originally refers to the input stream.
             * It must now refer to the corresponding output stream.
             */
            packet->stream_index = input.output_stream->index;

            /* Let the Matroska muxer perform the final interleaving. */
            check_ffmpeg(
                av_interleaved_write_frame(m_output_context, packet),
                "av_interleaved_write_frame");

            /* The packet has been consumed. */
            av_packet_unref(packet);
        }

        /* Finish Matroska file. */
        check_ffmpeg(
            av_write_trailer(m_output_context),
            "av_write_trailer");

        /* Cleanup input packets. */
        for (TrackFile& input : m_tracks)
        {
            av_packet_free(&input.packet);
            avformat_close_input(&input.format);
        }

        /* Cleanup output. */
        if (m_output_context != nullptr &&
            m_output_context->pb != nullptr &&
            !(m_output_context->oformat->flags & AVFMT_NOFILE))
        {
            avio_closep(&m_output_context->pb);
        }

        avformat_free_context(m_output_context);
        m_output_context = nullptr;
    }
    catch (...)
    {
        /* Cleanup on any error. */
        for (TrackFile& input : m_tracks)
        {
            av_packet_free(&input.packet);

            if (input.format != nullptr)
            {
                avformat_close_input(&input.format);
            }
        }


        if (m_output_context != nullptr)
        {
            if (m_output_context->pb != nullptr && !(m_output_context->oformat->flags & AVFMT_NOFILE))
            {
                avio_closep(&m_output_context->pb);
            }

            avformat_free_context(m_output_context);
        }

        throw;
    }
}
