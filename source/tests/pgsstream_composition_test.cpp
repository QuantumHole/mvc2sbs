// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "../pgsstream.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

static void put16(std::vector<uint8_t>& data, const uint16_t value)
{
	data.push_back(static_cast<uint8_t>(value >> 8));
	data.push_back(static_cast<uint8_t>(value));
}

static void put24(std::vector<uint8_t>& data, const uint32_t value)
{
	data.push_back(static_cast<uint8_t>(value >> 16));
	data.push_back(static_cast<uint8_t>(value >> 8));
	data.push_back(static_cast<uint8_t>(value));
}

static void put32(std::vector<uint8_t>& data, const uint32_t value)
{
	data.push_back(static_cast<uint8_t>(value >> 24));
	data.push_back(static_cast<uint8_t>(value >> 16));
	data.push_back(static_cast<uint8_t>(value >> 8));
	data.push_back(static_cast<uint8_t>(value));
}

static void appendSegment(
	std::vector<uint8_t>& stream,
	const uint32_t pts,
	const uint8_t type,
	const std::vector<uint8_t>& payload)
{
	stream.push_back('P');
	stream.push_back('G');
	put32(stream, pts);
	put32(stream, 0);
	stream.push_back(type);
	put16(stream, static_cast<uint16_t>(payload.size()));
	stream.insert(stream.end(), payload.begin(), payload.end());
}

static std::vector<uint8_t> makePcs(const uint32_t pts, const bool twoObjects)
{
	std::vector<uint8_t> payload;
	put16(payload, 1920);
	put16(payload, 1080);
	payload.push_back(0x10);
	put16(payload, 0);
	payload.push_back(0x80);
	payload.push_back(0);
	payload.push_back(0);
	payload.push_back(twoObjects ? 2 : 0);
	if (twoObjects)
	{
		put16(payload, 1);
		payload.insert(payload.end(), {0, 0});
		put16(payload, 10);
		put16(payload, 20);
		put16(payload, 2);
		payload.insert(payload.end(), {0, 0});
		put16(payload, 30);
		put16(payload, 40);
	}

	std::vector<uint8_t> segment;
	appendSegment(segment, pts, 0x16, payload);
	return segment;
}

static void appendObject(
	std::vector<uint8_t>& stream,
	const uint16_t objectId,
	const uint8_t colorIndex)
{
	std::vector<uint8_t> payload;
	put16(payload, objectId);
	payload.push_back(0);
	payload.push_back(0xc0);
	put24(payload, 5);
	put16(payload, 1);
	put16(payload, 1);
	payload.push_back(colorIndex);
	payload.push_back(0);
	payload.push_back(0);
	appendSegment(stream, 0, 0x15, payload);
}

int main()
{
	std::vector<uint8_t> input = makePcs(0, true);
	appendSegment(input, 0, 0x14, {0, 0, 1, 235, 128, 128, 255, 2, 81, 90, 240, 255});
	appendObject(input, 1, 1);
	appendObject(input, 2, 2);
	appendSegment(input, 0, 0x80, {});
	const std::vector<uint8_t> updatePcs = makePcs(90000, true);
	input.insert(input.end(), updatePcs.begin(), updatePcs.end());
	appendSegment(input, 90000, 0x80, {});
	const std::vector<uint8_t> clearPcs = makePcs(180000, false);
	input.insert(input.end(), clearPcs.begin(), clearPcs.end());
	appendSegment(input, 180000, 0x80, {});

	PGSStream stream;
	stream.push(input);
	assert(stream.hasNextComposition());
	PGSStream::PGSComposition composition = stream.getNextComposition();
	assert(composition.objects.size() == 2);
	assert(composition.screen.x == 1920);
	assert(std::abs(composition.time.s) < 0.000001);
	assert(std::abs(composition.time.t - 1.0) < 0.000001);
	assert(stream.hasNextComposition());
	PGSStream::PGSComposition nextComposition = stream.getNextComposition();
	assert(nextComposition.objects.size() == 2);
	assert(std::abs(nextComposition.time.s - 1.0) < 0.000001);
	assert(std::abs(nextComposition.time.t - 2.0) < 0.000001);
	assert(!stream.hasNextComposition());

	std::vector<PGSStream::PGSSegment> output = stream.saveComposition(composition, true);
	const std::vector<PGSStream::PGSSegment> nextOutput = stream.saveComposition(nextComposition, true);
	output.insert(output.end(), nextOutput.begin(), nextOutput.end());
	uint32_t previousPcsPts = 0;
	unsigned int pcsCount = 0;
	for (const PGSStream::PGSSegment& segment : output)
	{
		if (segment.payload[0] != 0x16)
		{
			continue;
		}

		const uint16_t width = static_cast<uint16_t>(
			(static_cast<uint16_t>(segment.payload[3]) << 8) | segment.payload[4]);
		const uint8_t objectCount = segment.payload[13];
		assert(segment.pts >= previousPcsPts);
		previousPcsPts = segment.pts;
		if (pcsCount == 0)
		{
			assert(width == 3840);
			assert(objectCount == 4);
		}
		else if (pcsCount == 1)
		{
			assert(segment.pts == 90000);
			assert(objectCount == 0);
		}
		else if (pcsCount == 2)
		{
			assert(segment.pts == 90000);
			assert(objectCount == 4);
		}
		else
		{
			assert(segment.pts == 180000);
			assert(objectCount == 0);
		}
		pcsCount++;
	}
	assert(pcsCount == 4);
	return 0;
}