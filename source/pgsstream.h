// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdint>
#include <vector>
#include <deque>
#include <map>
#include <stdexcept>
#include <algorithm>
#include <cstring>
// #include <glm/vec2.hpp>    // conflicts with countof() in EbmlConfig.h

class PGSStream {
	public:
		struct dvec2
		{
			double s = 0.0;
			double t = 0.0;
		};

		struct uvec2
		{
			uint16_t x = 0;
			uint16_t y = 0;
		};

		struct PGSFrame
		{
			dvec2 time = {0.0, 0.0};
			uvec2 screen = {0, 0};     // screen size
			uvec2 size = {0, 0};
			uvec2 position = {0, 0};
			std::vector<uint32_t> pixels = {};   // 32bit RGBA values
		};

		/** adds an arbitrary amount of data to the stream.
		 * If the stream contains a complete block, it is decoded as far as possible.
		 * Complete frames are available immediately when decoded.
		 */
		void push(const uint8_t* data, const size_t size);
		void push(const std::vector<uint8_t>& data);
		bool hasNextFrame(void) const;
		PGSFrame getNextFrame(void);
		void reset(void);
		std::vector<uint8_t> saveFrame(const PGSFrame& frame);

	private:
		typedef enum : uint8_t
		{
			PDS = 0x14,
			ODS = 0x15,
			PCS = 0x16,
			WDS = 0x17,
			END = 0x80
		} segment_t;

		typedef enum : uint8_t
		{
			COMPOSITION_NORMAL = 0x00,
			COMPOSITION_EPOCH_START = 0x80
		} composition_t;

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
			uint32_t pts = 0;
			uint16_t width = 0;
			uint16_t height = 0;
			uint8_t paletteId = 0;
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

		std::vector<uint8_t> m_input_data = {};
		std::deque<PGSFrame> m_frame_buffer = {};
		std::deque<PGSFrame> m_input_frames = {};

		std::map<uint8_t, Palette> m_palettes = {};
		std::map<uint16_t, Object> m_objects = {};
		std::vector<CompositionObject> m_compositions = {};
		uint16_t m_composition_id = 0;

		void parse(void);
		void processSegment(
			const uint8_t type,
			const uint32_t pts,
			const std::vector<uint8_t>& p);
		void readPCS(
			const uint32_t pts,
			const std::vector<uint8_t>& p);
		void readPDS(const std::vector<uint8_t>& p);
		void readODS(const std::vector<uint8_t>& p);
		void composeFrame(void);

		/** output functions */
		std::vector<uint8_t> makeIndexedPixels(
			const PGSFrame& frame,
			Palette& palette) const;
		void writeSegment(
			std::vector<uint8_t>& file,
			const uint32_t pts,
			const uint32_t dts,
			const segment_t type,
			const std::vector<uint8_t>& payload) const;
		void writePCS(
			std::vector<uint8_t>& file,
			const PGSFrame& frame,
			const uint16_t objectId,
			const uint32_t pts,
			const uint16_t compositionNumber,
			const uint8_t compositionState,
			const bool paletteOnlyUpdate,
			const uint8_t paletteId,
			const bool showObject) const;
		void writeWDS(
			std::vector<uint8_t>& file,
			const PGSFrame& frame,
			const uint32_t pts) const;
		void writePDS(
			std::vector<uint8_t>& file,
			const Palette& palette,
			const uint32_t pts,
			const uint8_t paletteId) const;
		void writeODS(
			std::vector<uint8_t>& file,
			const uint16_t objectId,
			const uint16_t width,
			const uint16_t height,
			const std::vector<uint8_t>& rle,
			const uint32_t pts) const;
		void writeFrameDisplaySet(
			std::vector<uint8_t>& file,
			const PGSFrame& frame,
			const uint32_t pts,
			const uint16_t compositionNumber,
			const bool epochStart) const;
};
