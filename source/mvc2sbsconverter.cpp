// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "mvc2sbsconverter.h"

extern "C" {
#include <libavutil/opt.h>
}

#include <iostream>

static const size_t MAX_NAL_SIZE = static_cast<size_t>(64) * 1024 * 1024;

MVC2SBSConverter::MVC2SBSConverter(std::ostream& output, const output_format_t format) :
	m_decoder(nullptr),
	m_output(output),
	m_input(),
	m_finished(false),
	m_y4mStarted(false),
	m_format(format),
	m_frame_index(0)
{
	Edge264MvcSettings settings;
	edge264mvc_default_settings(&settings);
	if (edge264mvc_open(&m_decoder, &settings) != EDGE264MVC_OK) {
		throw std::runtime_error("edge264mvc_open() failed");
	}
}

MVC2SBSConverter::~MVC2SBSConverter(void)
{
	if (m_decoder != nullptr)
	{
		edge264mvc_close(&m_decoder);
	}

	if (m_packet_av)
	{
		av_packet_free(&m_packet_av);
	}

	if (m_frame_av)
	{
		av_frame_free(&m_frame_av);
	}

	if (m_context_av)
	{
		avcodec_free_context(&m_context_av);
	}
}

size_t MVC2SBSConverter::findStartCode(const size_t from) const
{
	const size_t size = m_input.size();

	if (from >= size)
	{
		return size;
	}

	for (size_t i = from; i + 2 < size; i++)
	{
		if ((m_input[i] != 0) || (m_input[i + 1] != 0))
		{
			continue;
		}

		// 00 00 01
		if (m_input[i + 2] == 1)
		{
			return i;
		}

		// 00 00 00 01
		if ((m_input[i + 2] == 0) &&
		    (i + 3 < size) &&
		    (m_input[i + 3] == 1))
		{
			return i;
		}
	}

	return size;
}

bool MVC2SBSConverter::writeFrameH264(const Edge264MvcFrame& edge264frame)
{
	const bool mvc = (edge264frame.views[1].planes[0] != nullptr);

	/* For MVC we output both views side by side. */
	const int outputWidth = mvc ? edge264frame.width_Y * 2 : edge264frame.width_Y;
	const int outputHeight = edge264frame.height_Y;

	// initialization
	if (m_context_av == nullptr)
	{
		/*
		 * Frame rate from SPS VUI:
		 *
		 *   time_scale / (2 * num_units_in_tick)
		 *
		 * for a progressive stream.
		 *
		 * Fall back to 24000/1001 if the stream doesn't contain
		 * timing information.
		 */

		uint32_t ts = 0;
		uint32_t nu = 0;

		edge264mvc_get_timing(m_decoder, &ts, &nu);

		const bool haveRate = (ts != 0) && (nu != 0) && (nu < 0x40000000u);
		const int fnum = haveRate ? static_cast<int>(ts) : 24000;
		const int fden = haveRate ? static_cast<int>(2 * nu) : 1001;

		const AVCodec* codec = avcodec_find_encoder_by_name("libx264");

		if (!codec)
		{
			throw std::runtime_error("libx264-Encoder nicht verfügbar");
		}

		m_context_av = avcodec_alloc_context3(codec);

		if (!m_context_av)
		{
			throw std::runtime_error("avcodec_alloc_context3 fehlgeschlagen");
		}

		m_context_av->width = outputWidth;
		m_context_av->height = outputHeight;
		m_context_av->pix_fmt = AV_PIX_FMT_YUV420P;
		m_context_av->time_base = AVRational{fden, fnum};
		m_context_av->framerate = AVRational{fnum, fden};

		// Typische H.264-Einstellungen
		if ((av_opt_set(m_context_av->priv_data, "preset", "medium", AV_OPT_SEARCH_CHILDREN) < 0) ||
		    (av_opt_set(m_context_av->priv_data, "tune", "zerolatency", AV_OPT_SEARCH_CHILDREN) < 0) ||
		    (av_opt_set(m_context_av->priv_data, "crf", "18", AV_OPT_SEARCH_CHILDREN) < 0)
			// || (av_opt_set(m_context_av->priv_data, "annexb", "1", AV_OPT_SEARCH_CHILDREN) < 0)
		)
		{
			throw std::runtime_error("setting options failed");
		}

		if (avcodec_open2(m_context_av, codec, nullptr) < 0)
		{
			throw std::runtime_error("avcodec_open2 failed");
		}

		m_frame_av = av_frame_alloc();

		if (!m_frame_av)
		{
			throw std::runtime_error("av_frame_alloc failed");
		}

		m_frame_av->format = m_context_av->pix_fmt;
		m_frame_av->width  = outputWidth;
		m_frame_av->height = outputHeight;

		if (av_frame_get_buffer(m_frame_av, 0) < 0)
		{
			throw std::runtime_error("av_frame_get_buffer failed");
		}

		m_packet_av = av_packet_alloc();

		if (!m_packet_av)
		{
			throw std::runtime_error("av_packet_alloc fehlgeschlagen");
		}
	}

	// process frame
	if (av_frame_make_writable(m_frame_av) < 0)
	{
		throw std::runtime_error("av_frame_make_writable failed");
	}

	// copy data into h264 frame
	// Y
	for (int y = 0; y < edge264frame.height_Y; y++)
	{
		const uint8_t* left = edge264frame.views[0].planes[0] + static_cast<size_t>(y) * static_cast<size_t>(edge264frame.stride_Y);
		memcpy(m_frame_av->data[0] + y * m_frame_av->linesize[0], left, static_cast<size_t>(edge264frame.width_Y));

		if (mvc)
		{
			const uint8_t* right = edge264frame.views[1].planes[0] + static_cast<size_t>(y) * static_cast<size_t>(edge264frame.stride_Y);
			memcpy(m_frame_av->data[0] + y * m_frame_av->linesize[0] + edge264frame.width_Y, right, static_cast<size_t>(edge264frame.width_Y));
		}
	}

	// Cb
	for (int y = 0; y < edge264frame.height_C; y++)
	{
		const uint8_t* left = edge264frame.views[0].planes[1] + static_cast<size_t>(y) * static_cast<size_t>(edge264frame.stride_C);
		memcpy(m_frame_av->data[1] + y * m_frame_av->linesize[1], left, static_cast<size_t>(edge264frame.width_C));

		if (mvc)
		{
			const uint8_t* right = edge264frame.views[1].planes[1] + static_cast<size_t>(y) * static_cast<size_t>(edge264frame.stride_C);
			memcpy(m_frame_av->data[1] + y * m_frame_av->linesize[1] + edge264frame.width_C, right, static_cast<size_t>(edge264frame.width_C));
		}
	}

	// Cr
	for (int y = 0; y < edge264frame.height_C; y++)
	{
		const uint8_t* left = edge264frame.views[0].planes[2] + static_cast<size_t>(y) * static_cast<size_t>(edge264frame.stride_C);
		memcpy(m_frame_av->data[2] + y * m_frame_av->linesize[2], left, static_cast<size_t>(edge264frame.width_C));

		if (mvc)
		{
			const uint8_t* right = edge264frame.views[1].planes[2] + static_cast<size_t>(y) * static_cast<size_t>(edge264frame.stride_C);
			memcpy(m_frame_av->data[2] + y * m_frame_av->linesize[2] + edge264frame.width_C, right, static_cast<size_t>(edge264frame.width_C));
		}
	}

	m_frame_av->pts = static_cast<int64_t>(m_frame_index);   // frame index
	m_frame_index++;

	if (avcodec_send_frame(m_context_av, m_frame_av) < 0)
	{
		throw std::runtime_error("avcodec_send_frame failed");
	}

	while (true)
	{
		int ret = avcodec_receive_packet(m_context_av, m_packet_av);

		if ((ret == AVERROR(EAGAIN)) ||
		    (ret == AVERROR_EOF))
		{
			break;
		}

		if (ret < 0)
		{
			throw std::runtime_error("avcodec_receive_packet failed");
		}

		m_output.write(reinterpret_cast<const char*>(m_packet_av->data), m_packet_av->size);

		av_packet_unref(m_packet_av);
	}

	return true;
}

bool MVC2SBSConverter::writeFrameYUV4MPEG2(const Edge264MvcFrame& frame)
{
	const bool mvc = (frame.views[1].planes[0] != nullptr);

	// Write Y4M header exactly once.
	// This may be repeated at any position between frames,
	// but we only need it once.
	if (!m_y4mStarted)
	{
		/*
		 * Frame rate from SPS VUI:
		 *
		 *   time_scale / (2 * num_units_in_tick)
		 *
		 * for a progressive stream.
		 *
		 * Fall back to 24000/1001 if the stream doesn't contain
		 * timing information.
		 */

		uint32_t ts = 0;
		uint32_t nu = 0;

		edge264mvc_get_timing(m_decoder, &ts, &nu);

		const bool haveRate = (ts != 0) && (nu != 0) && (nu < 0x40000000u);
		const int fnum = haveRate ? static_cast<int>(ts) : 24000;
		const int fden = haveRate ? static_cast<int>(2 * nu) : 1001;

		/* For MVC we output both views side by side. */
		const int outputWidth = mvc ? frame.width_Y * 2 : frame.width_Y;

		m_output
		    << "YUV4MPEG2 "
		    << "W" << outputWidth
		    << " H" << frame.height_Y
		    << " F" << fnum << ":" << fden
		    << " Ip A1:1 C420mpeg2\n";

		if (!m_output)
		{
			return false;
		}

		m_y4mStarted = true;
	}

	// Frame header
	m_output << "FRAME\n";

	if (!m_output)
	{
		return false;
	}

	// Luma
	for (int y = 0; y < frame.height_Y; y++)
	{
		const uint8_t* left = frame.views[0].planes[0] + static_cast<size_t>(y) * static_cast<size_t>(frame.stride_Y);
		m_output.write(reinterpret_cast<const char*>(left), static_cast<std::streamsize>(frame.width_Y));

		if (mvc)
		{
			const uint8_t* right = frame.views[1].planes[0] + static_cast<size_t>(y) * static_cast<size_t>(frame.stride_Y);
			m_output.write(reinterpret_cast<const char*>(right), static_cast<std::streamsize>(frame.width_Y));
		}

		if (!m_output)
		{
			return false;
		}
	}

	// Cb
	for (int y = 0; y < frame.height_C; y++)
	{
		const uint8_t* left = frame.views[0].planes[1] + static_cast<size_t>(y) * static_cast<size_t>(frame.stride_C);
		m_output.write(reinterpret_cast<const char*>(left), static_cast<std::streamsize>(frame.width_C));

		if (mvc)
		{
			const uint8_t* right = frame.views[1].planes[1] + static_cast<size_t>(y) * static_cast<size_t>(frame.stride_C);
			m_output.write(reinterpret_cast<const char*>(right), static_cast<std::streamsize>(frame.width_C));
		}

		if (!m_output)
		{
			return false;
		}
	}

	// Cr
	for (int y = 0; y < frame.height_C; y++)
	{
		const uint8_t* left = frame.views[0].planes[2] + static_cast<size_t>(y) * static_cast<size_t>(frame.stride_C);
		m_output.write(reinterpret_cast<const char*>(left), static_cast<std::streamsize>(frame.width_C));

		if (mvc)
		{
			const uint8_t* right = frame.views[1].planes[2] + static_cast<size_t>(y) * static_cast<size_t>(frame.stride_C);
			m_output.write(reinterpret_cast<const char*>(right), static_cast<std::streamsize>(frame.width_C));
		}

		if (!m_output)
		{
			return false;
		}
	}

	return true;
}

bool MVC2SBSConverter::drainFrames(void)
{
	Edge264MvcFrame frame;
	bool decoding_ok = true;

	while (decoding_ok && edge264mvc_receive_frame(m_decoder, &frame) == EDGE264MVC_OK)
	{

		switch (m_format)
		{
			case FORMAT_YUV4MPEG2:
				if (!writeFrameYUV4MPEG2(frame))
				{
					decoding_ok = false;
				}
				break;
			case FORMAT_H264:
				if (!writeFrameH264(frame))
				{
					decoding_ok = false;
				}
				break;
			default:
				break;
		}
		edge264mvc_release_frame(m_decoder, &frame);
	}

	return decoding_ok;
}

bool MVC2SBSConverter::decodeNAL(const size_t begin, const size_t end)
{
	if (begin >= end)
	{
		return false;
	}

	const uint8_t* nal = m_input.data() + begin;
	unsigned stuck = 0;

	while (true)
	{
		const int res = edge264mvc_send_nal(m_decoder, nal, end - begin, 0, 0);
		const bool framesOk = drainFrames();

		if (!framesOk)
		{
			return false;
		}

		if (res == 0)
		{
			return true;
		}

		if (res == ENOBUFS)
		{
			// The decoder wants us to drain frames and retry.
			//
			// Normally drainFrames() above has made progress.
			if (stuck > 64)
			{
				return false;
			}
			stuck++;

			continue;
		}

		if (res == ENOTSUP)
		{
			// Unsupported NAL: ignore NAL and continue decoding.
			return true;
		}

		if (res == EBADMSG)
		{
			// A corrupt NAL. Continue with the next NAL.
			return true;
		}

		// Any other edge264 error is fatal.
		return false;
	}
}

bool MVC2SBSConverter::processInput(const bool endOfStream)
{
	while (true)
	{
		const size_t size = m_input.size();

		if (size == 0)
		{
			return true;
		}

		// Find first start code.
		const size_t start = findStartCode(0);

		if (start == size)
		{
			if (endOfStream)
			{
				// There is data left, but no start code.
				//
				// A stream consisting solely of leading zero bytes is
				// not a complete H.264 Annex-B stream.
				for (size_t i = 0; i < size; i++)
				{
					if (m_input[i] != 0)
					{
						return false;
					}
				}

				m_input.clear();
				return true;
			}

			// No complete start code yet.
			//
			// Keep the last three bytes because a start code may be
			// split over two packets:
			//
			// packet 1: 00 00
			// packet 2: 01 67 ...
			//
			if (size > 3)
			{
				for (size_t i = 0; i < size - 3; ++i)
				{
					if (m_input[i] != 0)
					{
						return false;
					}
				}

				m_input.erase(m_input.begin(), m_input.begin() + static_cast<std::ptrdiff_t>(size - 3));
			}

			return true;
		}

		// Only zero bytes may occur before the first start code.
		for (size_t i = 0; i < start; ++i)
		{
			if (m_input[i] != 0)
			{
				return false;
			}
		}

		// Determine start-code length.
		const size_t startCodeSize = ((start + 2 < size) && (m_input[start + 2] == 1)) ? 3 : 4;
		const size_t nalBegin = start + startCodeSize;

		// Start code without NAL data.
		if (nalBegin >= size)
		{
			if (endOfStream)
			{
				return false;
			}

			// Keep the start code and wait for another packet.
			if (start != 0)
			{
				m_input.erase(m_input.begin(), m_input.begin() + static_cast<std::ptrdiff_t>(start));
			}

			return true;
		}

		// Find next start code.
		const size_t nextStart = findStartCode(nalBegin);

		if (nextStart == size)
		{
			// No terminating start code yet.
			if (!endOfStream)
			{
				if (size - nalBegin > MAX_NAL_SIZE)
				{
					return false;
				}

				// Keep everything starting at the current start code.
				if (start != 0)
				{
					m_input.erase(m_input.begin(), m_input.begin() + static_cast<std::ptrdiff_t>(start));
				}

				return true;
			}

			// End of stream: this is the final NAL.
			if (size - nalBegin > MAX_NAL_SIZE)
			{
				return false;
			}

			if (!decodeNAL(nalBegin, size))
			{
				return false;
			}

			m_input.clear();

			return true;
		}

		// Empty NAL.
		if (nextStart <= nalBegin)
		{
			return false;
		}

		if (nextStart - nalBegin > MAX_NAL_SIZE)
		{
			return false;
		}

		// Decode complete NAL.
		if (!decodeNAL(nalBegin, nextStart))
		{
			return false;
		}

		// Keep the next start code for the next iteration.
		m_input.erase(m_input.begin(), m_input.begin() + static_cast<std::ptrdiff_t>(nextStart));
	}
}

bool MVC2SBSConverter::feed(const std::vector<uint8_t>& packet)
{
	if (m_finished)
	{
		return false;
	}

	if (packet.empty())
	{
		return true;
	}

	// Avoid an unreasonable amount of buffered data.
	if (m_input.size() > MAX_NAL_SIZE + 4 - packet.size())
	{
		return false;
	}

	m_input.insert(m_input.end(), packet.begin(), packet.end());

	return processInput(false);
}

bool MVC2SBSConverter::flush_yuv4mpeg2(void)
{
	/* Force edge264 to expose frames which are still delayed internally. */
	uint8_t dummy = 0;
	const uint8_t* begin = &dummy;
	// const uint8_t* end   = &dummy;

	for (unsigned i = 0; i < 256; i++)
	{
		const int res = edge264mvc_send_nal(m_decoder, begin, 0, 0, 0);

		if (!drainFrames())
		{
			return false;
		}

		if ((res == 0) ||
		    (res == ENOBUFS) ||
		    (res == ENOTSUP) ||
			(res == EBADMSG))
		{
			continue;
		}
		else if (res == ENODATA)
		{
			break;
		}

		return false;
	}

	edge264mvc_flush(m_decoder);

	return true;
}

bool MVC2SBSConverter::flush_h264(void)
{
	// Encoder flushen
	if (avcodec_send_frame(m_context_av, nullptr) < 0)
	{
		throw std::runtime_error("Encoder flush failed");
	}

	while (true)
	{
		int ret = avcodec_receive_packet(m_context_av, m_packet_av);

		if ((ret == AVERROR_EOF) ||
		    (ret == AVERROR(EAGAIN)))
		{
			break;
		}

		if (ret < 0)
		{
			throw std::runtime_error("avcodec_receive_packet (flush) failed");
		}

		m_output.write(reinterpret_cast<const char*>(m_packet_av->data), m_packet_av->size);

		av_packet_unref(m_packet_av);
	}
	return true;
}

bool MVC2SBSConverter::finish(void)
{
	if (m_finished)
	{
		return true;
	}

	if (!processInput(true))
	{
		return false;
	}

	edge264mvc_send_end(m_decoder);

	if (((m_format == FORMAT_YUV4MPEG2) && (!flush_yuv4mpeg2())) ||
	    ((m_format == FORMAT_H264) && (!flush_h264())))
	{
		return false;
	}

	m_finished = true;

	if (!m_input.empty())
	{
		return false;
	}

	return m_output.good();
}
