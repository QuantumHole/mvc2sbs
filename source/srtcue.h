// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SRTCUE_H
#define SRTCUE_H

#include <cstdint>
#include <string>
#include <vector>

/** subtitle segment. */
struct SRTSegment
{
	std::string text;          ///< subtitle text
	uint64_t startNs = 0;      ///< start timestamp in nanoseconds
	uint64_t durationNs = 0;   ///< duration of the subtitle in nanoseconds
};

/** Splits a subtitle text into segments for different speakers.
 * @param text input subtitle text.
 * @param startNs start timestamp in nanoseconds.
 * @param durationNs duration of the subtitle in nanoseconds.
 * @return list of SRT segments, each with its own text and timing information.
 */
std::vector<SRTSegment> splitSpeakers(
	const std::string& text,
	const uint64_t startNs,
	const uint64_t durationNs);

#endif
