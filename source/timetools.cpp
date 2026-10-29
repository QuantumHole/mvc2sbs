// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "timetools.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>

std::string timestamp(const double seconds)
{
	const uint64_t totalMs = static_cast<uint64_t>(seconds * 1000.0 + 0.5);

	const uint64_t hours = totalMs / 3600000;
	const uint64_t minutes = (totalMs / 60000) % 60;
	const uint64_t secs = (totalMs / 1000) % 60;
	const uint64_t millis = totalMs % 1000;

	std::ostringstream result;

	result << std::setfill('0')
	       << std::setw(2) << hours << ":"
	       << std::setw(2) << minutes << ":"
	       << std::setw(2) << secs << ","
	       << std::setw(3) << millis;

	return result.str();
}
