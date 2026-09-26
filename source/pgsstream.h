// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <vector>
#include <deque>
#include <map>
#include <stdexcept>
#include <algorithm>
#include <cstring>

class PGSStream {
	public:
		struct Frame
		{
			double time_show = 0.0;
			double time_hide = 0.0;
			uint16_t width = 0;
			uint16_t height = 0;

			// RGBA, row-major, width * height * 4 Bytes
			std::vector<uint32_t> pixels = {};
		};

		/*
		   Fügt einen beliebigen Datenblock hinzu.
		   Der Block darf mitten in einem PGS-Segment enden.
		 */
		void push(const uint8_t* data, const size_t size);
		void push(const std::vector<uint8_t>& data);
		bool hasNextFrame(void) const;
		Frame getNextFrame(void);
		void reset(void);

	private:
		enum : uint8_t
		{
			PDS = 0x14,
			ODS = 0x15,
			PCS = 0x16,
			WDS = 0x17,
			END = 0x80
		};

		typedef std::map<uint8_t, uint32_t> Palette;

		struct Object
		{
			uint16_t id = 0;
			uint16_t width = 0;
			uint16_t height = 0;
			std::vector<uint8_t> rle = {};
			bool complete = false;
		};

		struct CompositionObject
		{
			uint16_t objectId = 0;
			uint8_t windowId = 0;
			uint8_t cropped = 0;
			uint16_t x = 0;
			uint16_t y = 0;

			uint16_t cropX = 0;
			uint16_t cropY = 0;
			uint16_t cropW = 0;
			uint16_t cropH = 0;
		};

		struct DisplaySet
		{
			bool active = false;

			uint32_t pts90k = 0;
			uint32_t dts90k = 0;

			uint16_t width = 0;
			uint16_t height = 0;

			uint8_t paletteId = 0;
			std::vector<CompositionObject> compositionObjects;
		};

		const bool m_full_size = false;
		std::vector<uint8_t> input_ = {};
		std::deque<Frame> m_frame_buffer = {};
		std::deque<Frame> frames_ = {};

		std::map<uint8_t, Palette> palettes_ = {};
		std::map<uint16_t, Object> objects_ = {};
		DisplaySet current_ = {};

		void parse(void);
		void processSegment(
			const uint8_t type,
			const uint32_t pts,
			const uint32_t dts,
			const std::vector<uint8_t>& p);
		void processPcs(
			const uint32_t pts,
			const uint32_t dts,
			const std::vector<uint8_t>& p);
		void processPds(const std::vector<uint8_t>& p);
		void processOds(const std::vector<uint8_t>& p);
		void composeCurrentFrame(void);
		void decodeTextFrames(void);
};
