#ifndef TRACKMUXER_H
#define TRACKMUXER_H

#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>

// #include <libavcodec/avcodec.h>
// #include <libavutil/avutil.h>
// #include <libavutil/dict.h>
// #include <libavutil/error.h>
}

/** muxes a number of input files into a single Matroska file. */
class TrackMuxer
{
    private:

        /** information about one input file. */
        struct TrackFile
        {
            std::string language;   ///< track language.
            std::string title;      ///< track title.

            AVFormatContext* format = nullptr;
            AVStream* input_stream = nullptr;
            AVStream* output_stream = nullptr;
            AVPacket* packet = nullptr;
            bool eof = false;
        };
        std::vector<TrackFile> m_tracks;        ///< list of configured input tracks.
        AVFormatContext* m_output_context;      ///< output context.

        static TrackMuxer* m_instance;          ///< singleton instance pointer.

        /** private constructor.
         * @param file_name output file name.
         */
        explicit TrackMuxer(const std::string& file_name);

    public:

        /** instance getter.
         * @param file_name optional output file name,
         * only needed for the first instanciation of this class.
         * @return reference to the single instance.
         */
        static TrackMuxer& instance(const std::string& file_name = "");

        /** add an input file to the list of tracks.
         * @param file_name input file name.
         * @param track_index track index in input file.
         * @param format_name AV codec ID of the track in input file.
         * @param language language of the track.
         * @param title title for the track in output file.
         */
        void add_file(
            const std::string& file_name,
            const size_t track_index,
            const std::string& format_name,
            const std::string& language,
            const std::string& title);

        /** start muxing the configured tracks. */
        void mux(void);
};

#endif
