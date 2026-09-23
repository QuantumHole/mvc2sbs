#ifndef MKVSTREAMER_H
#define MKVSTREAMER_H

#include <string>

class MKVStreamer
{
    private:
        const std::string m_input_file_name;


    public:
        MKVStreamer(const std::string& filename);
        void run(void);
};

#endif
