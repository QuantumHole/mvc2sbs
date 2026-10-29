#include "../timetools.h"
#include "../srtcue.h"

#include <cassert>

int main()
{
	assert(timestamp(1.0) == "00:00:01,000");
	assert(timestamp(2.0) == "00:00:02,000");
	assert(timestamp(1.9996) == "00:00:02,000");

	const std::vector<SRTSegment> split = SRTCue::splitSpeakers(
		"- Hi\r\n continuation\r\n— Long reply", 10'000'000'000, 6'000'000'000);
	assert(split.size() == 2);
	assert(split[0].text == "Hi\n continuation");
	assert(split[1].text == "Long reply");
	assert(split[0].startNs == 10'000'000'000);
	assert(split[0].durationNs + split[1].durationNs == 6'000'000'000);
	assert(split[1].startNs == split[0].startNs + split[0].durationNs);
	assert(split[0].durationNs == 3'600'000'000);
	assert(split[1].durationNs == 2'400'000'000);

	const std::vector<SRTSegment> unicodeSplit = SRTCue::splitSpeakers(
		"– hallo\n— 世界", 0, 3'000'000'000);
	assert(unicodeSplit.size() == 2);
	assert(unicodeSplit[0].text == "hallo");
	assert(unicodeSplit[1].text == "世界");
	assert(unicodeSplit[0].durationNs + unicodeSplit[1].durationNs == 3'000'000'000);

	const std::vector<SRTSegment> unsplit = SRTCue::splitSpeakers(
		"Ordinary subtitle", 4'000'000'000, 2'000'000'000);
	assert(unsplit.size() == 1);
	assert(unsplit[0].text == "Ordinary subtitle");
	assert(unsplit[0].startNs == 4'000'000'000);
	assert(unsplit[0].durationNs == 2'000'000'000);

	const std::vector<SRTSegment> withPreamble = SRTCue::splitSpeakers(
		"Intro line\nmore intro\n- speaker", 0, 3'000'000'000);
	assert(withPreamble.size() == 2);
	assert(withPreamble[0].text == "Intro line\nmore intro");
	assert(withPreamble[1].text == "speaker");

	const std::vector<SRTSegment> emptyMarker = SRTCue::splitSpeakers(
		"-\n- actual text", 0, 1'000'000'000);
	assert(emptyMarker.size() == 1);
	assert(emptyMarker[0].text == "actual text");
	return 0;
}
