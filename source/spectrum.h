// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include <vector>
#include <string>

/** class for handling color spectra. */
class Spectrum
{
	private:

		/** structure representing a color. */
		typedef struct
		{
			unsigned char r;    ///< red component of the color.
			unsigned char g;    ///< green component of the color.
			unsigned char b;    ///< blue component of the color.
		}
		color_t;

		std::vector<color_t> m_lattice;          ///< collection of fixed colors forming the spectrum.

		/** converts a color to its hexadecimal string representation.
		 * @param color input color.
		 * @return hexadecimal string representing the color.
		 */
		std::string color_hex(const color_t& color) const;

		/** interpolates between two colors based on a fractional value.
		 * @param a first color.
		 * @param b second color.
		 * @param frac fractional value for interpolation.
		 * @return interpolated color.
		 */
		color_t interpolate_vector(const color_t& a, const color_t& b, const double frac) const;

	public:

		/** constructor. */
		Spectrum(void);

		/** generates a collection of colors forming a spectrum.
		 * @param n number of colors to generate.
		 * @return vector of hexadecimal strings representing the colors in the spectrum.
		 */
		std::vector<std::string> colors(const size_t n) const;
};
