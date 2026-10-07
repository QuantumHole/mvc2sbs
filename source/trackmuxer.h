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
            std::string filename;   ///< input file name.
            AVMediaType type;       ///< track type.
            std::string language;   ///< track language.
            std::string title;      ///< track title.
            size_t stream_index;    ///< track index in input file.

            AVFormatContext* format = nullptr;
            AVStream* input_stream = nullptr;
            AVStream* output_stream = nullptr;
            AVPacket* packet = nullptr;
            bool eof = false;
        };
        std::vector<TrackFile> m_tracks;        ///< list of configured input tracks.
        const std::string m_output_file_name;   ///< output file name for muxed file.
        AVFormatContext* m_output_context = nullptr;

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
         * @param type type of the track in input file.
         * @param language language of the track.
         * @param title title for the track in output file.
         * @param index optional track index in input file.
         */
        void add_file(
            const std::string& file_name,
            const AVMediaType type,
            const std::string& language,
            const std::string& title,
            const size_t index = 0);

        /** start muxing the configured tracks. */
        void mux(void);
};

#endif
