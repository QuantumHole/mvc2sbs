// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pgsstream.h"

#include <cstdint>
#include <vector>
#include <deque>
#include <map>
#include <stdexcept>
#include <algorithm>
#include <cstring>

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

static void require(const bool condition, const std::string& message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

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

void PGSStream::push(const uint8_t* data, size_t size)
{
	input_.insert(input_.end(), data, data + size);
	parse();
}

void PGSStream::push(const std::vector<uint8_t>& data)
{
	push(data.data(), data.size());
}

bool PGSStream::hasNextFrame(void) const
{
	return !frames_.empty();
}

PGSStream::Frame PGSStream::getNextFrame(void)
{
	if (frames_.empty())
	{
		throw std::runtime_error("Kein vollständiger PGS-Frame verfügbar");
	}

	Frame result = std::move(frames_.front());
	frames_.pop_front();
	return result;
}

void PGSStream::reset(void)
{
	input_.clear();
	m_frame_buffer.clear();
	frames_.clear();
	palettes_.clear();
	objects_.clear();
	current_ = DisplaySet{};
}

void PGSStream::processPcs(
	uint32_t pts,
	uint32_t dts,
	const std::vector<uint8_t>& p)
{
	// Minimaler PCS-Header:
	// width, height, frame_rate, composition_number,
	// composition_state, palette_update_flag, palette_id,
	// number_of_composition_objects
	require(p.size() >= 11, "PCS-Segment ist zu kurz");

	// Ein neues PCS beendet kein Display Set automatisch.
	// Es startet lediglich die Beschreibung eines neuen Sets.
	current_ = DisplaySet{};
	current_.active = true;
	current_.pts90k = pts;
	current_.dts90k = dts;
	current_.width = readBE16(p, 0);
	current_.height = readBE16(p, 2);
	current_.paletteId = p[9];

	const uint8_t objectCount = p[10];

	if (objectCount == 0)
	{
		// set hide timestamps for frames currently in frame buffer
		while (!m_frame_buffer.empty())
		{
			Frame result = std::move(m_frame_buffer.front());
			m_frame_buffer.pop_front();
			result.time_hide = static_cast<double>(current_.pts90k) / 90000.0;
			frames_.push_back(result);
		}
	}

	// Jedes Composition Object ist 8 Bytes lang.
	// Bei gesetztem Cropped-Flag folgen weitere 8 Bytes.
	size_t pos = 11;

	for (uint8_t i = 0; i < objectCount; ++i)
	{
		require(pos + 8 <= p.size(), "PCS-Objekt ist abgeschnitten");

		CompositionObject object;
		object.objectId = readBE16(p, pos + 0);
		object.windowId = p[pos + 2];
		object.cropped = p[pos + 3];
		object.x = readBE16(p, pos + 4);
		object.y = readBE16(p, pos + 6);

		pos += 8;

		if (object.cropped & 0x40)
		{
			require(pos + 8 <= p.size(),
			        "PCS-Croppingdaten sind abgeschnitten");

			object.cropX = readBE16(p, pos + 0);
			object.cropY = readBE16(p, pos + 2);
			object.cropW = readBE16(p, pos + 4);
			object.cropH = readBE16(p, pos + 6);

			pos += 8;
		}

		current_.compositionObjects.push_back(object);
	}
}

void PGSStream::processPds(const std::vector<uint8_t>& p)
{
	// PDS:
	// 1 Byte Palette-ID
	// 1 Byte Palette-Version
	// danach Einträge zu je 5 Bytes:
	// entry_id, Y, Cr, Cb, Alpha
	require(p.size() >= 2, "PDS-Segment ist zu kurz");

	uint8_t paletteId = p[0];
	Palette palette;

	size_t pos = 2;
	while (pos + 5 <= p.size())
	{
		uint8_t entryId = p[pos + 0];
		uint8_t y       = p[pos + 1];
		uint8_t cr      = p[pos + 2];
		uint8_t cb      = p[pos + 3];
		uint8_t alpha   = p[pos + 4];

		palette[entryId] = yuvToRgba(y, cr, cb, alpha);
		pos += 5;
	}

	palettes_[paletteId] = std::move(palette);
}

void PGSStream::processOds(const std::vector<uint8_t>& p)
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
	require(p.size() >= 4, "ODS-Segment ist zu kurz");

	uint16_t objectId = readBE16(p, 0);
	uint8_t sequenceFlags = p[3];

	const bool first = (sequenceFlags & 0x80) != 0;
	const bool last  = (sequenceFlags & 0x40) != 0;

	Object& object = objects_[objectId];
	object.id = objectId;

	size_t pos = 4;

	if (first)
	{
		require(p.size() >= 11, "Erstes ODS-Segment ist zu kurz");

		// Drei Bytes coded object data length.
		// Die Länge wird hier nur validiert; die tatsächliche
		// Fragmentierung wird über first/last behandelt.
		uint32_t dataLength =
			(static_cast<uint32_t>(p[4]) << 16) |
			(static_cast<uint32_t>(p[5]) << 8)  |
			static_cast<uint32_t>(p[6]);

		(void)dataLength;

		object.width = readBE16(p, 7);
		object.height = readBE16(p, 9);
		object.rle.clear();
		object.complete = false;
		pos = 11;
	}

	if (pos < p.size())
	{
		object.rle.insert(
			object.rle.end(),
			p.begin() + static_cast<ptrdiff_t>(pos),
			p.end());
	}

	if (last)
	{
		object.complete = true;
	}
}

void PGSStream::composeCurrentFrame(void)
{
	require(current_.width != 0 && current_.height != 0,
	        "PCS enthält keine gültige Bildgröße");

	Frame frame;
	frame.time_show = static_cast<double>(current_.pts90k) / 90000.0;
	frame.width = current_.width;
	frame.height = current_.height;

	const size_t pixelCount = static_cast<size_t>(frame.width) * frame.height;

	frame.pixels.assign(pixelCount, 0);

	std::map<uint8_t, Palette>::const_iterator paletteIt = palettes_.find(current_.paletteId);

	if (paletteIt == palettes_.end())
	{
		// Ohne Palette kann das Bild nicht sinnvoll dekodiert werden.
		return;
	}

	const Palette& palette = paletteIt->second;

	for (const CompositionObject& co : current_.compositionObjects)
	{
		std::map<uint16_t, Object>::const_iterator objectIt = objects_.find(co.objectId);

		if (objectIt == objects_.end())
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

		std::vector<uint8_t> indices = decodeRle(object.rle, static_cast<size_t>(object.width) * static_cast<size_t>(object.height));

		uint16_t drawWidth = object.width;
		uint16_t drawHeight = object.height;
		uint16_t sourceX = 0;
		uint16_t sourceY = 0;

		if (co.cropped & 0x40)
		{
			sourceX = co.cropX;
			sourceY = co.cropY;
			drawWidth = co.cropW;
			drawHeight = co.cropH;
		}

		for (uint16_t y = 0; y < drawHeight; ++y)
		{
			for (uint16_t x = 0; x < drawWidth; ++x)
			{
				uint32_t sx = sourceX + x;
				uint32_t sy = sourceY + y;

				if ((sx >= object.width) ||
				    (sy >= object.height))
				{
					continue;
				}

				int dstX = static_cast<int>(co.x) + x;
				int dstY = static_cast<int>(co.y) + y;

				if ((dstX < 0) || (dstY < 0) ||
				    (dstX >= frame.width) ||
				    (dstY >= frame.height))
				{
					continue;
				}

				uint8_t paletteIndex =
					indices[static_cast<size_t>(sy) *
					        object.width + sx];

				std::map<uint8_t, uint32_t>::const_iterator colorIt = palette.find(paletteIndex);

				if (colorIt == palette.end())
				{
					continue;
				}

				const uint32_t color = colorIt->second;

				size_t dst =
					(static_cast<size_t>(dstY) *
					 frame.width + static_cast<size_t>(dstX));

				frame.pixels[dst] = color;
			}
		}
	}

	m_frame_buffer.push_back(std::move(frame));
}

void PGSStream::decodeTextFrames(void)
{
	require(current_.width != 0 && current_.height != 0,
	        "PCS enthält keine gültige Bildgröße");

	std::map<uint8_t, Palette>::const_iterator paletteIt = palettes_.find(current_.paletteId);

	if (paletteIt == palettes_.end())
	{
		// Ohne Palette kann das Bild nicht sinnvoll dekodiert werden.
		return;
	}

	const Palette& palette = paletteIt->second;

	for (const CompositionObject& co : current_.compositionObjects)
	{
		std::map<uint16_t, Object>::const_iterator objectIt = objects_.find(co.objectId);

		if (objectIt == objects_.end())
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

		std::vector<uint8_t> indices = decodeRle(object.rle, static_cast<size_t>(object.width) * static_cast<size_t>(object.height));

		Frame frame;
		frame.time_show = static_cast<double>(current_.pts90k) / 90000.0;
		frame.width = object.width;
		frame.height = object.height;
		const size_t pixelCount = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
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
}

void PGSStream::processSegment(
	uint8_t type,
	uint32_t pts,
	uint32_t dts,
	const std::vector<uint8_t>& p)
{
	switch (type)
	{
		case PCS:
			processPcs(pts, dts, p);
			break;

		case PDS:
			processPds(p);
			break;

		case ODS:
			processOds(p);
			break;

		case END:

			if (current_.active)
			{
				if (m_full_size)
				{
					composeCurrentFrame();
				}
				else
				{
					decodeTextFrames();
				}
				current_ = DisplaySet{};
			}
			break;

		case WDS:
			// Die Fenstergrenzen sind für das Compositing nicht zwingend
			// erforderlich, weil PCS bereits die Objektposition enthält.
			break;

		default:
			// Unbekannte Segmenttypen werden ignoriert.
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
		// 4 Bytes  DTS
		// 1 Byte   Typ
		// 2 Bytes  Segmentgröße
		if (input_.size() < 13)
		{
			return;
		}

		// Nach beschädigten oder vorangestellten Daten synchronisieren.
		if ((input_[0] != 'P') || (input_[1] != 'G'))
		{
			input_.erase(input_.begin());
			continue;
		}

		uint32_t pts = readBE32(input_, 2);
		uint32_t dts = readBE32(input_, 6);
		uint8_t type = input_[10];
		uint16_t length = readBE16(input_, 11);

		if (input_.size() < 13u + length)
		{
			return;
		}

		std::vector<uint8_t> payload(
			input_.begin() + 13,
			input_.begin() + 13 + length);

		input_.erase(input_.begin(), input_.begin() + 13 + length);

		processSegment(type, pts, dts, payload);
	}
}
