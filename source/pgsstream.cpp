// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "pgsstream.h"

#include <cstdint>
#include <vector>
#include <deque>
#include <map>
#include <stdexcept>
#include <algorithm>
#include <cstring>

#include <array>
#include <fstream>
#include <limits>
#include <unordered_map>

static uint16_t readBE16(const std::vector<uint8_t>& data, const size_t offset)
{
	if (offset + 2 > data.size())
	{
		throw std::runtime_error("Ungültiger 16-Bit-Zugriff");
	}

	return static_cast<uint16_t>(
		(static_cast<uint16_t>(data[offset]) << 8) |
		static_cast<uint16_t>(data[offset + 1])
		);
}

static uint32_t readBE32(const std::vector<uint8_t>& data, const size_t offset)
{
	if (offset + 4 > data.size())
	{
		throw std::runtime_error("Ungültiger 32-Bit-Zugriff");
	}

	return (static_cast<uint32_t>(data[offset]) << 24) |
	       (static_cast<uint32_t>(data[offset + 1]) << 16) |
	       (static_cast<uint32_t>(data[offset + 2]) << 8)  |
	       static_cast<uint32_t>(data[offset + 3]);
}

static void putBE16(std::vector<uint8_t>& out, uint16_t v)
{
	out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
	out.push_back(static_cast<uint8_t>(v & 0xff));
}

static void putBE24(std::vector<uint8_t>& out, uint32_t v)
{
	out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
	out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
	out.push_back(static_cast<uint8_t>(v & 0xff));
}

static void putBE32(std::vector<uint8_t>& out, uint32_t v)
{
	out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
	out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
	out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
	out.push_back(static_cast<uint8_t>(v & 0xff));
}

static void require(const bool condition, const std::string& message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

static uint32_t toPts(const double seconds)
{
	if (seconds <= 0.0)
	{
		return 0;
	}

	const double value = seconds * 90000.0;

	if (value >= static_cast<double>(std::numeric_limits<uint32_t>::max()))
	{
		return std::numeric_limits<uint32_t>::max();
	}

	return static_cast<uint32_t>(value + 0.5);
}

/** wandelt einen Yuv Wert in 32bit RGBA im Format 0xAARRGGBB um. */
static uint32_t yuvToRgba(
	const uint8_t y,
	const uint8_t cr,
	const uint8_t cb,
	const uint8_t alpha)
{
	// PGS verwendet nominale YCrCb-Werte.
	// Die Berechnung wird auf den Bereich 0..255 begrenzt.
	const double Y  = static_cast<double>(y);
	const double Cr = static_cast<double>(cr) - 128.0;
	const double Cb = static_cast<double>(cb) - 128.0;

	const uint32_t r = static_cast<uint32_t>(Y + 1.40200 * Cr);
	const uint32_t g = static_cast<uint32_t>(Y - 0.34414 * Cb - 0.71414 * Cr);
	const uint32_t b = static_cast<uint32_t>(Y + 1.77200 * Cb);

	return (static_cast<uint32_t>(alpha) << 24) |
	       (static_cast<uint32_t>(r) << 16) |
	       (static_cast<uint32_t>(g) << 8)  |
	       static_cast<uint32_t>(b);
}

// ARGB -> YCbCrA gemäß üblicher BT.601-Konvertierung.
static uint32_t rgbaToYuvA(uint32_t rgba)
{
	const double r = static_cast<uint8_t>((rgba >> 16) & 0xff);
	const double g = static_cast<uint8_t>((rgba >> 8) & 0xff);
	const double b = static_cast<uint8_t>(rgba & 0xff);

	const double y  =  0.257 * r + 0.504 * g + 0.098 * b + 16.0;
	const double cb = -0.148 * r - 0.291 * g + 0.439 * b + 128.0;
	const double cr =  0.439 * r - 0.368 * g - 0.071 * b + 128.0;

	const uint8_t alpha = static_cast<uint8_t>((rgba >> 24) & 0xff);
	const uint8_t y8  = static_cast<uint8_t>(std::clamp( y, 0.0, 255.0) + 0.5);
	const uint8_t cr8 = static_cast<uint8_t>(std::clamp(cr, 0.0, 255.0) + 0.5);
	const uint8_t cb8 = static_cast<uint8_t>(std::clamp(cb, 0.0, 255.0) + 0.5);

	return (static_cast<uint32_t>(y8) << 24) |
	       (static_cast<uint32_t>(cr8) << 16) |
	       (static_cast<uint32_t>(cb8) << 8)  |
	       static_cast<uint32_t>(alpha);
}

static std::vector<uint8_t> decodeRle(const std::vector<uint8_t>& rle, const size_t frame_size)
{
	std::vector<uint8_t> data(frame_size, 0);

	size_t source = 0;
	size_t target = 0;

	while (source < rle.size() && target < frame_size)
	{
		const uint8_t value = rle[source];
		source++;

		if (value != 0)
		{
			// Nicht-null: einzelner Pixel mit Palette-Index value.
			data[target] = value;
			target++;
			continue;
		}

		// Null leitet ein RLE-Steuerwort ein.
		if (source >= rle.size())
		{
			break;
		}

		const uint8_t control = rle[source];
		source++;

		if (control == 0)
		{
			// Zeilenende.
			continue;
		}

		size_t runLength = 0;
		uint8_t color = 0;

		if (control & 0x40)
		{
			// 0x40: zusätzliches Längenbyte
			if (source >= rle.size())
			{
				break;
			}

			runLength = (static_cast<size_t>(control & 0x3F) << 8) | rle[source];
			source++;
		}
		else
		{
			runLength = control & 0x3F;
		}

		if (control & 0x80)
		{
			// Farbindex folgt.
			if (source >= rle.size())
			{
				break;
			}

			color = rle[source];
			source++;
		}
		else
		{
			// Transparenter Run.
			color = 0;
		}

		std::fill_n(data.begin() + static_cast<ptrdiff_t>(target), runLength, color);
		target += runLength;
	}

	return data;
}

/*
    PGS-RLE:

    - Nicht-null Byte: direktes Farbindex-Byte
    - 00 00: Zeilenende
    - 00 80..8f xx color: Wiederholung
        Länge = ((byte2 & 0x0f) << 8) | byte3
 */
static std::vector<uint8_t> encodeRle(
	const size_t width,
	const size_t height,
	const std::vector<uint8_t>& indexed)
{
	if ((width == 0) || (height == 0))
	{
		throw std::runtime_error("empty frame");
	}

	if (indexed.size() != width * height)
	{
		throw std::runtime_error("pixel data size mismatch");
	}

	std::vector<uint8_t> rle;

	for (size_t y = 0; y < height; ++y)
	{
		size_t x = 0;

		while (x < width)
		{
			const uint8_t color = indexed[y * width + x];

			size_t run = 1;
			while (x + run < width &&
			       indexed[y * width + x + run] == color)
			{
				++run;
			}

			size_t remaining = run;

			while (remaining > 0)
			{
				const size_t part = std::min<size_t>(remaining, 0x3fff);

				if ((color != 0) && (part < 3))
				{
					// Einzelne Pixel bzw. 2er-Läufe können direkt
					// als Farbindex codiert werden.
					for (size_t i = 0; i < part; ++i)
					{
						rle.push_back(color);
					}
				}
				else if (color == 0)
				{
					if (part <= 0x3f)
					{
						// kurze transparente Folge
						rle.push_back(0x00);
						rle.push_back(static_cast<uint8_t>(part));
					}
					else
					{
						// lange transparente Folge
						rle.push_back(0x00);
						rle.push_back(
							static_cast<uint8_t>(0x40 | ((part >> 8) & 0x3f)));
						rle.push_back(
							static_cast<uint8_t>(part & 0xff));
					}
				}
				else
				{
					if (part <= 0x3f)
					{
						// kurze farbige Folge
						rle.push_back(0x00);
						rle.push_back(
							static_cast<uint8_t>(0x80 | part));
						rle.push_back(color);
					}
					else
					{
						// lange farbige Folge
						rle.push_back(0x00);
						rle.push_back(
							static_cast<uint8_t>(
								0xc0 | ((part >> 8) & 0x3f)));
						rle.push_back(
							static_cast<uint8_t>(part & 0xff));
						rle.push_back(color);
					}
				}

				remaining -= part;
			}

			x += run;
		}

		// End of line
		rle.push_back(0x00);
		rle.push_back(0x00);
	}

	// Wichtig: Das ist "end of object", nicht END des Display Sets.
	rle.push_back(0x00);
	rle.push_back(0x01);

	return rle;
}

void PGSStream::push(const uint8_t* data, const size_t size)
{
	m_input_data.insert(m_input_data.end(), data, data + size);
	parse();
}

void PGSStream::push(const std::vector<uint8_t>& data)
{
	push(data.data(), data.size());
}

bool PGSStream::hasNextFrame(void) const
{
	return !m_input_frames.empty();
}

PGSStream::PGSFrame PGSStream::getNextFrame(void)
{
	if (m_input_frames.empty())
	{
		throw std::runtime_error("Kein vollständiger PGS-Frame verfügbar");
	}

	PGSFrame result = std::move(m_input_frames.front());
	m_input_frames.pop_front();
	return result;
}

void PGSStream::reset(void)
{
	m_input_data.clear();
	m_frame_buffer.clear();
	m_input_frames.clear();
	m_palettes.clear();
	m_objects.clear();
	m_compositions.clear();
	m_composition_id = 0;
}

void PGSStream::readPCS(
	const uint32_t pts,
	const std::vector<uint8_t>& p)
{
	// Minimal PCS-Header:
	// width, height, frame_rate, composition_number,
	// composition_state, palette_update_flag, palette_id,
	// number_of_composition_objects
	require(p.size() >= 11, "PCS too short");

	const uint8_t objectCount = p[10];

	if (objectCount == 0)
	{
		// set hide timestamps for all frames currently in frame buffer
		while (!m_frame_buffer.empty())
		{
			PGSFrame result = std::move(m_frame_buffer.front());
			m_frame_buffer.pop_front();
			result.time.t = static_cast<double>(pts) / 90000.0;
			m_input_frames.push_back(result);
		}
	}

	// A new PCS does not automatically end any previous Display Set.
	// PCS only describe sets of displayed elements.
	const uint16_t ds_width = readBE16(p, 0);
	const uint16_t ds_height = readBE16(p, 2);
	const uint8_t ds_paletteId = p[9];

	require(ds_width != 0 && ds_height != 0, "PCS contains invalid frame size");

	// Every Composition Object is 8 bytes long,
	// followed by 8 additional bytes with set Cropped flag.
	size_t pos = 11;
	for (uint8_t i = 0; i < objectCount; i++)
	{
		require(pos + 8 <= p.size(), "PCS objekt is incomplete");

		CompositionObject co;
		co.pts = pts;
		co.width = ds_width;
		co.height = ds_height;
		co.paletteId = ds_paletteId;
		co.objectId = readBE16(p, pos + 0);
		co.windowId = p[pos + 2];
		co.cropped = p[pos + 3];
		co.x = readBE16(p, pos + 4);
		co.y = readBE16(p, pos + 6);

		pos += 8;

		if (co.cropped & 0x40)
		{
			require(pos + 8 <= p.size(), "PCS cropping data is incomplete");

			co.cropX = readBE16(p, pos + 0);
			co.cropY = readBE16(p, pos + 2);
			co.cropW = readBE16(p, pos + 4);
			co.cropH = readBE16(p, pos + 6);

			pos += 8;
		}

		m_compositions.push_back(co);
	}
}

void PGSStream::readPDS(const std::vector<uint8_t>& p)
{
	// PDS:
	// 1 Byte Palette-ID
	// 1 Byte Palette-Version
	// thereafter entries of 5 bytes each:
	// entry_id, Y, Cr, Cb, Alpha
	require(p.size() >= 2, "PDS too short");
	require((p.size() - 2) % 5 == 0, "PDS size mismatch");

	const uint8_t paletteId = p[0];
	Palette palette;
	size_t pos = 2;
	while (pos + 5 <= p.size())
	{
		const uint8_t entryId = p[pos + 0];
		const uint8_t y       = p[pos + 1];
		const uint8_t cr      = p[pos + 2];
		const uint8_t cb      = p[pos + 3];
		const uint8_t alpha   = p[pos + 4];

		palette[entryId] = yuvToRgba(y, cr, cb, alpha);
		pos += 5;
	}

	m_palettes[paletteId] = std::move(palette);
}

void PGSStream::readODS(const std::vector<uint8_t>& p)
{
	// ODS:
	// object_id       2 Bytes
	// object_version  1 Byte
	// sequence_flags  1 Byte
	//
	// Bei first-in-sequence folgen:
	// object_data_length 3 Bytes
	// object_width       2 Bytes
	// object_height      2 Bytes
	//
	// Der restliche Inhalt ist RLE-Daten.
	require(p.size() >= 4, "ODS too short");

	const uint16_t objectId = readBE16(p, 0);
	const uint8_t sequenceFlags = p[3];
	const bool first = (sequenceFlags & 0x80) != 0;
	const bool last  = (sequenceFlags & 0x40) != 0;

	Object& object = m_objects[objectId];
	object.id = objectId;
	size_t pos = 4;

	if (first)
	{
		require(p.size() >= 11, "first ODS too short");

		// p[4-6] encodes data object length.
		// data object length is ignored here; the actual
		// fragmentation is handled via first/last.
		// uint32_t dataLength =
		// (static_cast<uint32_t>(p[4]) << 16) |
		// (static_cast<uint32_t>(p[5]) << 8)  |
		// static_cast<uint32_t>(p[6]);

		object.width = readBE16(p, 7);
		object.height = readBE16(p, 9);
		object.rle.clear();
		object.complete = false;
		pos = 11;
	}

	if (pos < p.size())
	{
		object.rle.insert(object.rle.end(), p.begin() + static_cast<ptrdiff_t>(pos), p.end());
	}

	if (last)
	{
		object.complete = true;
	}
}

void PGSStream::composeFrame(void)
{
	for (const CompositionObject& co : m_compositions)
	{
		std::map<uint8_t, Palette>::const_iterator paletteIt = m_palettes.find(co.paletteId);

		if (paletteIt == m_palettes.end())
		{
			// image without palette cannot be decoded.
			continue;
		}
		const Palette& palette = paletteIt->second;

		std::map<uint16_t, Object>::const_iterator objectIt = m_objects.find(co.objectId);

		if (objectIt == m_objects.end())
		{
			continue;
		}
		const Object& object = objectIt->second;

		if (!object.complete ||
		    (object.width == 0) ||
		    (object.height == 0))
		{
			continue;
		}

		const size_t pixelCount = static_cast<size_t>(object.width) * static_cast<size_t>(object.height);
		std::vector<uint8_t> indices = decodeRle(object.rle, pixelCount);

		PGSFrame frame;
		frame.time.s = static_cast<double>(co.pts) / 90000.0;
		frame.size.x = object.width;
		frame.size.y = object.height;
		frame.screen.x = co.width;
		frame.screen.y = co.height;

		if (co.cropped & 0x40)
		{
			frame.position.x = co.cropX;
			frame.position.y = co.cropY;
		}
		else
		{
			frame.position.x = co.x;
			frame.position.y = co.y;
		}

		frame.pixels.resize(pixelCount);

		for (size_t x = 0; x < indices.size(); x++)
		{
			const uint8_t paletteIndex = indices[x];
			std::map<uint8_t, uint32_t>::const_iterator colorIt = palette.find(paletteIndex);
			const uint32_t color = (colorIt == palette.end()) ? 0 : colorIt->second;
			frame.pixels[x] = color;
		}
		m_frame_buffer.push_back(std::move(frame));
	}
	m_compositions.clear();
}

void PGSStream::processSegment(
	const uint8_t type,
	const uint32_t pts,
	const std::vector<uint8_t>& p)
{
	switch (type)
	{
		case PCS:
			readPCS(pts, p);
			break;

		case PDS:
			readPDS(p);
			break;

		case ODS:
			readODS(p);
			break;

		case END:
			composeFrame();
			// compose frame this late, because PCS is usually stored first,
			// with referenced palette or object segments stored later.
			break;

		case WDS:
			// window definition segment is not necessary for compositing,
			// because the presentation compositionn segment already
			// contains object positions.
			break;

		default:
			// ignore unknown segment types.
			break;
	}
}

void PGSStream::parse(void)
{
	while (true)
	{
		// Ein PGS-Segmentheader benötigt 13 Bytes:
		//
		// 2 Bytes  magic "PG"
		// 4 Bytes  PTS
		// 4 Bytes  DTS    (ignored)
		// 1 Byte   Typ
		// 2 Bytes  Segmentgröße
		if (m_input_data.size() < 13)
		{
			return;
		}

		// Nach beschädigten oder vorangestellten Daten synchronisieren.
		if ((m_input_data[0] != 'P') || (m_input_data[1] != 'G'))
		{
			m_input_data.erase(m_input_data.begin());
			continue;
		}

		const uint32_t pts = readBE32(m_input_data, 2);
		// const uint32_t dts = readBE32(m_input_data, 6);
		const uint8_t type = m_input_data[10];
		const uint16_t length = readBE16(m_input_data, 11);

		if (m_input_data.size() < 13u + length)
		{
			return;
		}

		std::vector<uint8_t> payload(
			m_input_data.begin() + 13,
			m_input_data.begin() + 13 + length);

		m_input_data.erase(m_input_data.begin(), m_input_data.begin() + 13 + length);

		processSegment(type, pts, payload);
	}
}

std::vector<uint8_t> PGSStream::makeIndexedPixels(
	const PGSFrame& frame,
	Palette& palette) const
{
	std::unordered_map<uint32_t, uint8_t> lookup;
	std::vector<uint8_t> indexed;

	indexed.reserve(frame.pixels.size());

	for (uint32_t rgba : frame.pixels)
	{
		const std::unordered_map<uint32_t, uint8_t>::const_iterator it = lookup.find(rgba);

		if (it == lookup.end())
		{
			const uint8_t i = static_cast<uint8_t>(palette.size() + 1);
			lookup.emplace(rgba, i);
			palette.emplace(i, rgba);
			indexed.push_back(i);
		}
		else
		{
			indexed.push_back(it->second);
		}
	}

	return indexed;
}

void PGSStream::writeSegment(
	std::vector<uint8_t>& buffer,
	const uint32_t pts,
	const uint32_t dts,
	const segment_t type,
	const std::vector<uint8_t>& payload,
	const bool header) const
{
	if (payload.size() > 0xffff)
	{
		throw std::runtime_error("PGS-Segment ist größer als 65535 Bytes");
	}

	// Segmentkopf:
	// "PG", PTS, DTS, Typ, Länge
	if (header)
	{
		buffer.push_back('P');
		buffer.push_back('G');

		putBE32(buffer, pts);
		putBE32(buffer, dts);
	}

	buffer.push_back(type);
	putBE16(buffer, static_cast<uint16_t>(payload.size()));

	buffer.insert(buffer.end(), payload.begin(), payload.end());
}

void PGSStream::writePCS(
	std::vector<uint8_t>& buffer,
	const PGSFrame& frame,
	const uint16_t objectId,
	const uint32_t pts,
	const uint16_t compositionNumber,
	const composition_t compositionState,
	const bool paletteOnlyUpdate,
	const uint8_t paletteId,
	const uint8_t frame_copies,
	const bool header) const
{
	std::vector<uint8_t> payload;

	putBE16(payload, frame.screen.x * std::max<uint8_t>(1, frame_copies));
	putBE16(payload, frame.screen.y);

	// Frame rate. 0x10 = 24 fps / üblicher Defaultwert.
	payload.push_back(0x10);

	putBE16(payload, compositionNumber);
	payload.push_back(compositionState);

	payload.push_back(paletteOnlyUpdate ? 0x80 : 0x00);
	payload.push_back(paletteId);

	// number of Composition Objects
	payload.push_back(frame_copies);

	for (uint16_t i = 0; i < frame_copies; i++)
	{
		// Object ID
		putBE16(payload, objectId);

		// Window ID
		payload.push_back(0);

		// Object Cropping Flag + Reserved
		payload.push_back(0x00);

		// Cropping-Werte werden nur bei gesetztem Cropping Flag benötigt.
		putBE16(payload, frame.position.x + i * frame.screen.x);
		putBE16(payload, frame.position.y);
	}

	writeSegment(buffer, pts, 0, PCS, payload, header);
}

void PGSStream::writeWDS(
	std::vector<uint8_t>& buffer,
	const PGSFrame& frame,
	const uint32_t pts,
	const bool header) const
{
	std::vector<uint8_t> payload;

	// Anzahl der Windows
	payload.push_back(1);

	// Window ID
	payload.push_back(0);

	// Window X, Y, Width, Height
	putBE16(payload, frame.position.x);
	putBE16(payload, frame.position.y);
	putBE16(payload, frame.size.x);
	putBE16(payload, frame.size.y);

	writeSegment(buffer, pts, 0, WDS, payload, header);
}

void PGSStream::writePDS(
	std::vector<uint8_t>& buffer,
	const Palette& palette,
	const uint32_t pts,
	const uint8_t paletteId,
	const bool header) const
{
	std::vector<uint8_t> payload;

	// Palette ID
	payload.push_back(paletteId);

	// Palette-Version
	payload.push_back(0);

	for (Palette::const_iterator iter = palette.begin(); iter != palette.end(); ++iter)
	{
		const uint32_t yuva = rgbaToYuvA(iter->second);

		payload.push_back(static_cast<uint8_t>(iter->first));   // Entry ID
		payload.push_back(static_cast<uint8_t>(yuva >> 24));    // Y
		payload.push_back(static_cast<uint8_t>(yuva >> 16));    // Cr
		payload.push_back(static_cast<uint8_t>(yuva >> 8));     // Cb
		payload.push_back(static_cast<uint8_t>(yuva));          // Alpha
	}

	writeSegment(buffer, pts, 0, PDS, payload, header);
}

void PGSStream::writeODS(
	std::vector<uint8_t>& buffer,
	const uint16_t objectId,
	const uint16_t width,
	const uint16_t height,
	const std::vector<uint8_t>& rle,
	const uint32_t pts,
	const bool header) const
{
	/*
	    ODS-Daten:

	    Object ID       2 Bytes
	    Version         1 Byte
	    Sequence Flags  1 Byte
	    Data Length     3 Bytes

	    Bei einem First-Fragment folgen zusätzlich:
	    Width           2 Bytes
	    Height          2 Bytes
	 */

	constexpr size_t maxPayload = 0xffff;
	constexpr size_t odsHeader = 11; // 2+1+1+3+2+2

	size_t offset = 0;
	uint8_t version = 0;

	while (offset < rle.size())
	{
		const bool first = offset == 0;

		// Payload-Größe einschließlich ODS-Kopfdaten
		const size_t headerSize = first ? odsHeader : 7;
		const size_t maxData = maxPayload - headerSize;
		const size_t chunk = std::min(maxData, rle.size() - offset);

		std::vector<uint8_t> payload;

		putBE16(payload, objectId); // Object ID
		payload.push_back(version++);

		uint8_t sequence = 0;

		if (first)
		{
			sequence |= 0x80; // first in sequence
		}

		if (offset + chunk == rle.size())
		{
			sequence |= 0x40; // last in sequence
		}
		payload.push_back(sequence);

		const uint32_t dataLength =
			static_cast<uint32_t>(chunk + (first ? 4 : 0));

		putBE24(payload, dataLength);

		if (first)
		{
			putBE16(payload, width);
			putBE16(payload, height);
		}

		payload.insert(
			payload.end(),
			rle.begin() + static_cast<std::ptrdiff_t>(offset),
			rle.begin() + static_cast<std::ptrdiff_t>(offset + chunk));

		writeSegment(buffer, pts, 0, ODS, payload, header);

		offset += chunk;
	}
}

std::vector<uint8_t> PGSStream::saveFrame(const PGSFrame& frame, const bool sbs_double,	const bool header)
{
	std::vector<uint8_t> payload;

	if ((frame.time.s < 0.0) || (frame.time.t <= frame.time.s))
	{
		throw std::runtime_error("invalid frame timestamps");
	}

	if ((frame.screen.x == 0) || (frame.screen.y == 0))
	{
		throw std::runtime_error("invalid screen size");
	}

	if ((frame.size.x == 0) || (frame.size.y == 0))
	{
		throw std::runtime_error("invalid frame size");
	}

	if ((frame.position.x + frame.size.x > frame.screen.x) ||
	    (frame.position.y + frame.size.y > frame.screen.y))
	{
		throw std::runtime_error("frame not contained in screen");
	}

	const uint32_t startPts = toPts(frame.time.s);
	const uint32_t endPts   = toPts(frame.time.t);
	const composition_t compositionState = (m_composition_id == 0) ? COMPOSITION_EPOCH_START : COMPOSITION_NORMAL;

	Palette palette;
	const std::vector<uint8_t> indexed = makeIndexedPixels(frame, palette);
	const std::vector<uint8_t> rle = encodeRle(frame.size.x, frame.size.y, indexed);

	const uint16_t objectId = 0;
	writePCS(payload, frame, objectId, startPts, m_composition_id, compositionState, false, 0, sbs_double ? 2 : 1, header);
	m_composition_id++;
	writeWDS(payload, frame, startPts, header);
	writePDS(payload, palette, startPts, 0, header);
	writeODS(payload, objectId, frame.size.x, frame.size.y, rle, startPts, header);
	writeSegment(payload, startPts, 0, END, {}, header);

	// write empty display set to clear all subtitles.
	writePCS(payload, frame, objectId, endPts, m_composition_id, COMPOSITION_NORMAL, false, 0, 0, header);
	m_composition_id++;
	writeSegment(payload, endPts, 0, END, {}, header);

	return payload;
}
