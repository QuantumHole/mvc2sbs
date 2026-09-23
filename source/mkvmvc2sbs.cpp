#include "mkvstreamer.h"

#include <string>
#include <iostream>

int main(const int argc, const char** argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: " << argv[0] << " <file.mkv>" << std::endl;
		return 1;
	}

	const std::string filename = argv[1];

	MKVStreamer mkv(filename);
	mkv.run();

	return 0;
}
