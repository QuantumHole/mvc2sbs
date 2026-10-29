// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "srtcue.h"

#include <cmath>
#include <cstddef>
#include <utility>

struct SRTLine
{
	std::string text = {};
	size_t markerLength = 0;
	size_t markerEnd = 0;
};

static std::vector<SRTLine> splitLines(const std::string& text)
{
	std::vector<SRTLine> lines;
	size_t start = 0;

	while (start < text.size())
	{
		const size_t end = text.find('\n', start);
		const size_t length = (end == std::string::npos) ? (text.size() - start) : (end - start);
		SRTLine line;
		line.text = text.substr(start, length);

		if (!line.text.empty() && (line.text.back() == '\r'))
		{
			line.text.pop_back();
		}

		size_t marker = 0;
		while (marker < line.text.size() &&
		       ((line.text[marker] == ' ') || (line.text[marker] == '\t')))
		{
			++marker;
		}

		if ((marker < line.text.size()) && (line.text[marker] == '-'))
		{
			line.markerLength = 1;
		}
		else if ((marker + 3 <= line.text.size()) &&
		         (static_cast<unsigned char>(line.text[marker]) == 0xe2) &&
		         (static_cast<unsigned char>(line.text[marker + 1]) == 0x80) &&
		         ((static_cast<unsigned char>(line.text[marker + 2]) == 0x93) ||
		          (static_cast<unsigned char>(line.text[marker + 2]) == 0x94)))
		{
			line.markerLength = 3;
		}

		if (line.markerLength != 0)
		{
			line.markerEnd = marker + line.markerLength;
			while (line.markerEnd < line.text.size() &&
			       (line.text[line.markerEnd] == ' ' || line.text[line.markerEnd] == '\t'))
			{
				++line.markerEnd;
			}
		}

		lines.push_back(std::move(line));

		if (end == std::string::npos)
		{
			break;
		}
		start = end + 1;
	}
	return lines;
}

static uint64_t characterCount(const std::string& text)
{
	uint64_t count = 0;

	for (const char byte : text)
	{
		if (((byte & 0xc0) != 0x80) && (byte != '\n') && (byte != '\r'))
		{
			++count;
		}
	}
	return count;
}

std::vector<SRTSegment> splitSpeakers(
	const std::string& text,
	const uint64_t startNs,
	const uint64_t durationNs)
{
	const std::vector<SRTLine> lines = splitLines(text);
	bool hasMarker = false;

	for (const SRTLine& line : lines)
	{
		hasMarker = hasMarker || line.markerLength != 0;
	}

	if (!hasMarker)
	{
		return {{text, startNs, durationNs}};
	}

	std::vector<std::string> texts;
	std::string current;
	bool hasCurrent = false;
	for (const SRTLine& line : lines)
	{
		if (line.markerLength != 0)
		{
			if (hasCurrent && (characterCount(current) != 0))
			{
				texts.push_back(std::move(current));
				current.clear();
			}
			current = line.text.substr(line.markerEnd);
			hasCurrent = true;
		}
		else
		{
			if (hasCurrent)
			{
				current.push_back('\n');
			}
			current += line.text;
			hasCurrent = true;
		}
	}

	if (hasCurrent && (characterCount(current) != 0))
	{
		texts.push_back(std::move(current));
	}

	if (texts.empty())
	{
		return {{std::string(), startNs, durationNs}};
	}

	if (texts.size() == 1)
	{
		return {{std::move(texts.front()), startNs, durationNs}};
	}

	uint64_t totalWeight = 0;
	for (const std::string& segmentText : texts)
	{
		totalWeight += characterCount(segmentText);
	}

	if (totalWeight == 0)
	{
		return {{std::string(), startNs, durationNs}};
	}

	const uint64_t totalMilliseconds = durationNs / 1000000 +
	                                   ((durationNs % 1000000 >= 500000) ? 1 : 0);
	std::vector<SRTSegment> segments;
	segments.reserve(texts.size());
	uint64_t cumulativeWeight = 0;
	uint64_t previousBoundary = 0;
	for (size_t index = 0; index < texts.size(); ++index)
	{
		cumulativeWeight += characterCount(texts[index]);
		const uint64_t boundary = (index + 1 == texts.size())
		    ? totalMilliseconds
		    : static_cast<uint64_t>(std::floor(
										static_cast<long double>(totalMilliseconds) * cumulativeWeight /
										static_cast<long double>(totalWeight) + 0.5L));
		const uint64_t offsetNs = previousBoundary * 1000000;
		segments.push_back({
			std::move(texts[index]),
			startNs + offsetNs,
			(boundary - previousBoundary) * 1000000});
		previousBoundary = boundary;
	}
	return segments;
}
