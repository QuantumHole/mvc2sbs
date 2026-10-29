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

/** reads and writes subtitel frames in PGS format. */
class PGSStream {
	public:
		/** two component vector of doubles.
		 * Used to store a range in time for the display a subtitle.
		 */
		struct dvec2
		{
			double s = 0.0;
			double t = 0.0;
		};

		/** two component vector of integers.
		 * Used to store a pair of coordinates, i.e. a point on screen or the size of an area.
		 */
		struct uvec2
		{
			uint16_t x = 0;
			uint16_t y = 0;
		};

		/** single PGS subtitle, including timing information,
		 * size, position, and actual image data.
		 */
		struct PGSFrame
		{
			dvec2 time = {0.0, 0.0};
			uvec2 screen = {0, 0};     // screen size
			uvec2 size = {0, 0};
			uvec2 position = {0, 0};
			std::vector<uint32_t> pixels = {};   // 32bit RGBA values
		};

		/** complete visible state described by one PCS. */
		struct PGSComposition
		{
			dvec2 time = {0.0, 0.0};
			uvec2 screen = {0, 0};
			std::vector<PGSFrame> objects = {};
		};

		/** PGS segment buffer. */
		struct PGSSegment
		{
			uint32_t pts = 0;
			uint32_t dts = 0;
			std::vector<uint8_t> payload = {};
		};

		/** serialized display set with its presentation timestamp. */
		struct PGSDisplaySet
		{
			uint32_t pts = 0;
			std::vector<uint8_t> data = {};
		};

		/** adds an arbitrary amount of data to the stream.
		 * If the stream contains a complete block, it is decoded as far as possible.
		 * Complete frames are available immediately when decoded.
		 * @param data pointer to unstructured data.
		 * @param size number of bytes in data.
		 */
		void push(const uint8_t* data, const size_t size);

		/** adds an arbitrary amount of data to the stream.
		 * If the stream contains a complete block, it is decoded as far as possible.
		 * Complete frames are available immediately when decoded.
		 * @param data vector of unstructured data.
		 */
		void push(const std::vector<uint8_t>& data);

		/** reports when there are decoded PCS compositions from the input stream.
		 * @return indicates that a complete composition is ready to be processed.
		 */
		bool hasNextComposition(void) const;

		/** retrieves one decoded composition for processing.
		 * @return the decoded composition.
		 */
		PGSComposition getNextComposition(void);

		/** resets all buffers and counters for a new input stream. */
		void reset(void);

		/** encode one complete PCS composition as PGS display sets.
		 * @param composition composition to be stored.
		 * @param sbs_double flag for doubling all subtitles for SBS frame layout.
		 * @return list of PGS segments.
		 */
		std::vector<PGSSegment> saveComposition(
			const PGSComposition& composition,
			const bool sbs_double);

		/** serialize segment into a stream of bytes.
		 * @param segment input segment.
		 * @param header header (ID, PTS, DTS) is only written when true.
		 * @return serialized segment.
		 */
		std::vector<uint8_t> serializeSegment(const PGSSegment& segment, const bool header) const;

		/** serialize consecutive segments with matching timestamps as display sets.
		 * @param segments list of PGS segments.
		 * @return list of display sets, each with its own timestamp.
		 */
		std::vector<PGSDisplaySet> serializeDisplaySets(
			const std::vector<PGSSegment>& segments) const;

	private:
		/** segments stored inside a PGS stream. */
		typedef enum : uint8_t
		{
			PDS = 0x14,        ///< palette definition segment
			ODS = 0x15,        ///< object definition segment
			PCS = 0x16,        ///< presentation composition segment
			WDS = 0x17,        ///< window definition segment
			END = 0x80         ///< end of PCS definition
		} segment_t;

		/** type of composition timestamp. */
		typedef enum : uint8_t
		{
			COMPOSITION_NORMAL = 0x00,             ///< start regular PGS segment, which may require all previous segments.
			COMPOSITION_ACQUISITION_POINT = 0x40,  ///< a complete PGS state may be aquired from this point on, e.g. after a jump to this position.
			COMPOSITION_EPOCH_START = 0x80         ///< composition epoch start time.
		} composition_t;

		/** associates indices with RGBA color information. */
		typedef std::map<uint8_t, uint32_t> Palette;

		/** image stored in PGS RLE format. */
		struct Object
		{
			uint16_t id = 0;
			uint16_t width = 0;
			uint16_t height = 0;
			std::vector<uint8_t> rle = {};
			bool complete = false;
		};

		/** composition information for one PGS subtitle.
		 * This includes its display time (not hide time),
		 * selected palette, or cropping information.
		 */
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

		std::vector<uint8_t> m_input_data = {};               ///< buffer of input data.
		std::deque<PGSComposition> m_frame_buffer = {};        ///< decoded compositions awaiting their hide timestamp.
		std::deque<PGSComposition> m_input_frames = {};        ///< complete compositions with timing information.

		std::map<uint8_t, Palette> m_palettes = {};           ///< palette buffer for multiple palettes.
		std::map<uint16_t, Object> m_objects = {};            ///< object buffer for multiple subtitle objects.
		std::vector<CompositionObject> m_compositions = {};   ///< current compositions, usually one object is sufficient.
		uint16_t m_composition_id = 0;                        ///< composition counter for stream output.

		/** parse the input stream for completed segments. */
		void parse(void);

		/** process a single segment detected in parse().
		 * @param type segment type.
		 * @param pts display timestamp, only needed for PCS.
		 * @param p segment payload extracted from input stream.
		 */
		void processSegment(const uint8_t type, const uint32_t pts, const std::vector<uint8_t>& p);

		/** Read presentation composition segement.
		 * Translate defined objects into frames.
		 * Frame hide time is set, when the next empty composition object is received.
		 * @param  pts display timestamp.
		 * @param p segment payload extracted from input stream.
		 */
		void readPCS(const uint32_t pts, const std::vector<uint8_t>& p);

		/** read palette definition segment.
		 * @param p segment payload extracted from input stream.
		 */
		void readPDS(const std::vector<uint8_t>& p);

		/** read Object Definition Segment.
		 * @param p segment payload extracted from input stream.
		 */
		void readODS(const std::vector<uint8_t>& p);

		/** compose frame from previously received PCS, ODS, PDS, and WDS. */
		void composeFrame(void);

		/** convert RGBA pixels of frame into indexed array with palette.
		 * @param frame input frame.
		 * @param palette palette constructed from the frame pixel data.
		 * @return frame pixels encoded as palette indices.
		 */
		std::vector<uint8_t> makeIndexedPixels(const PGSFrame& frame, Palette& palette) const;

		/** write general PGS segment into buffer.
		 * @param pts display timestamp of segment.
		 * @param dts decoding timestamp of segment, may always be zero.
		 * @param type segment type.
		 * @param payload segment data.
		 * @return PGS segment container.
		 */
		PGSSegment writeSegment(
			const uint32_t pts,
			const uint32_t dts,
			const segment_t type,
			const std::vector<uint8_t>& payload) const;

		/** write Presentation Composition Segment.
		 * @param frame current frame to be encoded.
		 * @param objectId ID of object.
		 * @param pts display timestamp of segment.
		 * @param compositionNumber composition index.
		 * @param compositionState timestamp reference.
		 * @param paletteOnlyUpdate always false, write full palette.
		 * @param paletteId palette ID, usually zero.
		 * @param frame_copies number of copies to display. 0 means: hide all previous frames.
		 * 1: show regular frame. 2: double the frame width and show frame on both SBS images.
		 * @return PGS segment container.
		 */
		PGSSegment writePCS(
			const PGSFrame& frame,
			const uint16_t objectId,
			const uint32_t pts,
			const uint16_t compositionNumber,
			const composition_t compositionState,
			const bool paletteOnlyUpdate,
			const uint8_t paletteId,
			const uint8_t frame_copies) const;

		/** write Presentation Composition Segment.
		 * @param composition complete composition to be encoded.
		 * @param objectIds list of object IDs to be displayed.
		 * @param pts display timestamp of segment.
		 * @param compositionNumber composition index.
		 * @param compositionState timestamp reference.
		 * @param paletteId palette ID, usually zero.
		 * @param frame_copies number of copies to display. 0 means: hide all previous frames.
		 * 1: show regular frame. 2: double the frame width and show frame on both SBS images.
		 * @return PGS segment container.
		 */
		PGSSegment writePCS(
			const PGSComposition& composition,
			const std::vector<uint16_t>& objectIds,
			const uint32_t pts,
			const uint16_t compositionNumber,
			const composition_t compositionState,
			const uint8_t paletteId,
			const uint8_t frame_copies) const;

		/** write Window Definition Segment.
		 * @param frame current frame to be encoded.
		 * @param pts display timestamp of segment.
		 * @return PGS segment container.
		 */
		PGSSegment writeWDS(
			const PGSFrame& frame,
			const uint32_t pts) const;

		/** write Window Definition Segment.
		 * @param width width of window.
		 * @param height height of window.
		 * @param pts display timestamp of segment.
		 * @return PGS segment container.
		 */
		PGSSegment writeWDS(
			const uint16_t width,
			const uint16_t height,
			const uint32_t pts) const;

		/** write Palette Definition Segment.
		 * @param palette palette to be stored in segment.
		 * @param pts display timestamp of segment.
		 * @param paletteId palette ID, usually zero.
		 * @return PGS segment container.
		 */
		PGSSegment writePDS(
			const Palette& palette,
			const uint32_t pts,
			const uint8_t paletteId) const;

		/** write Object Definition Segment containing an RLE encoded frame.
		 * @param objectId ID of object.
		 * @param width width of object.
		 * @param height height of object.
		 * @param rle RLE encoded palette image.
		 * @param pts display timestamp of segment.
		 * @return list of PGS segment containers.
		 */
		std::vector<PGSSegment> writeODS(
			const uint16_t objectId,
			const uint16_t width,
			const uint16_t height,
			const std::vector<uint8_t>& rle,
			const uint32_t pts) const;
};
