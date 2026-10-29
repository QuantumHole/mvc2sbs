#include "../timetools.h"

#include <cassert>

int main()
{
	assert(timestamp(1.0) == "00:00:01,000");
	assert(timestamp(2.0) == "00:00:02,000");
	assert(timestamp(1.9996) == "00:00:02,000");
	return 0;
}