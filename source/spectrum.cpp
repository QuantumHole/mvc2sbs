// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include <sstream>
#include <iomanip>
#include "spectrum.h"

Spectrum::Spectrum(void) :
	m_lattice()
{
	// private const lattice = array(
// array(158, 1, 66),
// array(213, 62, 79),
// array(244, 109, 67),
// array(253, 174,  97),
// array(254, 224, 139),
////	array(255, 255, 191),
////	array(230, 245, 152),
// array(171, 221, 164),
// array(102, 194, 165),
// array(50, 136, 189),
// array(94, 79, 162),
// array(158, 1, 66)

	color_t c;

	c = {255, 100, 100};
	m_lattice.push_back(c);
	c = {0, 200, 0};
	m_lattice.push_back(c);
	c = {50, 50, 255};
	m_lattice.push_back(c);
	// c = {255, 0, 0};
	// m_lattice.push_back(c);
}

std::string Spectrum::color_hex(const color_t& color) const
{
	std::stringstream s;

	s << std::setw(2) << std::setfill('0') << std::hex << static_cast<size_t>(color.r);
	s << std::setw(2) << std::setfill('0') << std::hex << static_cast<size_t>(color.g);
	s << std::setw(2) << std::setfill('0') << std::hex << static_cast<size_t>(color.b);
	return s.str();
}

Spectrum::color_t Spectrum::interpolate_vector(const color_t& a, const color_t& b, const double frac) const
{
	color_t mix;

	mix.r = static_cast<unsigned char>(frac * b.r + (1.0 - frac) * a.r);
	mix.g = static_cast<unsigned char>(frac * b.g + (1.0 - frac) * a.g);
	mix.b = static_cast<unsigned char>(frac * b.b + (1.0 - frac) * a.b);
	return mix;
}

std::vector<std::string> Spectrum::colors(const size_t n) const
{
	std::vector<std::string> map;

	for (size_t i = 0; i < n; i++)
	{
		double s = static_cast<double>(i * m_lattice.size()) / static_cast<double>(n);

		size_t s_int = static_cast<size_t>(s);
		double frac = s - static_cast<double>(s_int);

		size_t index2;

		if (s_int + 1 < m_lattice.size())
		{
			index2 = s_int + 1;
		}
		else
		{
			index2 = 0;
		}
		color_t c1 = m_lattice[s_int];
		color_t c2 = m_lattice[index2];
		color_t color = interpolate_vector(c1, c2, frac);
		std::string mix = color_hex(color);
		map.push_back(mix);
	}
	return map;
}
