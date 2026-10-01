#include "mvc2sbsconverter.h"

extern "C" {
#include <libavutil/opt.h>
}

#include <iostream>

static const size_t MAX_NAL_SIZE = static_cast<size_t>(64) * 1024 * 1024;

/** constructor.
 * @param output output stream.
 */
MVC2SBSConverter::MVC2SBSConverter(std::ostream& output, const output_format_t format) :
	decoder_(nullptr),
	output_(output),
	input_(),
	consumed_(0),
	finished_(false),
	y4mStarted_(false),
	skippedCorrupt_(0),
	m_format(format),
	m_frame_index(0)
{
	const int num_threads = -1;     // automatic number of threads

	decoder_ = edge264_alloc(num_threads, nullptr, nullptr, false, nullptr, nullptr, nullptr);

	if (decoder_ == nullptr)
	{
		throw std::runtime_error("edge264_alloc() failed");
	}
}

/** destructor. */
MVC2SBSConverter::~MVC2SBSConverter(void)
{
	if (decoder_ != nullptr)
	{
		edge264_free(&decoder_);
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

/** look for Annex-B start-code (00 00 01 or 00 00 00 01)
 * in input data stream.
 * @param from start index in input data.
 * @return index of start code.
 */
size_t MVC2SBSConverter::findStartCode(const size_t from) const
{
	const size_t size = input_.size();

	if (from >= size)
	{
		return size;
	}

	for (size_t i = from; i + 2 < size; i++)
	{
		if ((input_[i] != 0) || (input_[i + 1] != 0))
		{
			continue;
		}

		// 00 00 01
		if (input_[i + 2] == 1)
		{
			return i;
		}

		// 00 00 00 01
		if ((input_[i + 2] == 0) &&
		    (i + 3 < size) &&
		    (input_[i + 3] == 1))
		{
			return i;
		}
	}

	return size;
}

/** write complete frame into output stream.
 * The output format is H.264.
 * @param edge264frame completely decoded frame.
 * @return success status of writing frame into output stream.
 */
bool MVC2SBSConverter::writeFrameH264(const Edge264Frame& edge264frame)
{
	const bool mvc = (edge264frame.samples_mvc[0] != nullptr);
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

		edge264_get_timing(decoder_, &ts, &nu);

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
		    (av_opt_set(m_context_av->priv_data, "crf", "18", AV_OPT_SEARCH_CHILDREN) < 0))
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
		const uint8_t* left = edge264frame.samples[0] + static_cast<size_t>(y) * static_cast<size_t>(edge264frame.stride_Y);
		memcpy(m_frame_av->data[0] + y * m_frame_av->linesize[0], left, static_cast<size_t>(edge264frame.width_Y));

		if (mvc)
		{
			const uint8_t* right = edge264frame.samples_mvc[0] + static_cast<size_t>(y) * static_cast<size_t>(edge264frame.stride_Y);
			memcpy(m_frame_av->data[0] + y * m_frame_av->linesize[0] + edge264frame.width_Y, right, static_cast<size_t>(edge264frame.width_Y));
		}
	}

	// Cb
	for (int y = 0; y < edge264frame.height_C; y++)
	{
		const uint8_t* left = edge264frame.samples[1] + static_cast<size_t>(y) * static_cast<size_t>(edge264frame.stride_C);
		memcpy(m_frame_av->data[1] + y * m_frame_av->linesize[1], left, static_cast<size_t>(edge264frame.width_C));

		if (mvc)
		{
			const uint8_t* right = edge264frame.samples_mvc[1] + static_cast<size_t>(y) * static_cast<size_t>(edge264frame.stride_C);
			memcpy(m_frame_av->data[1] + y * m_frame_av->linesize[1] + edge264frame.width_C, right, static_cast<size_t>(edge264frame.width_C));
		}
	}

	// Cr
	for (int y = 0; y < edge264frame.height_C; y++)
	{
		const uint8_t* left = edge264frame.samples[2] + static_cast<size_t>(y) * static_cast<size_t>(edge264frame.stride_C);
		memcpy(m_frame_av->data[2] + y * m_frame_av->linesize[2], left, static_cast<size_t>(edge264frame.width_C));

		if (mvc)
		{
			const uint8_t* right = edge264frame.samples_mvc[2] + static_cast<size_t>(y) * static_cast<size_t>(edge264frame.stride_C);
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

		output_.write(reinterpret_cast<const char*>(m_packet_av->data), m_packet_av->size);

		av_packet_unref(m_packet_av);
	}

	return true;
}

/** write complete frame into output stream.
 * The output format is YUV4MPEG2.
 * @param frame completely decoded frame.
 * @return success status of writing frame into output stream.
 */
bool MVC2SBSConverter::writeFrameYUV4MPEG2(const Edge264Frame& frame)
{
	const bool mvc = (frame.samples_mvc[0] != nullptr);

	// Write Y4M header exactly once.
	// This may be repeated at any position between frames,
	// but we only need it once.
	if (!y4mStarted_)
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

		edge264_get_timing(decoder_, &ts, &nu);

		const bool haveRate = (ts != 0) && (nu != 0) && (nu < 0x40000000u);
		const int fnum = haveRate ? static_cast<int>(ts) : 24000;
		const int fden = haveRate ? static_cast<int>(2 * nu) : 1001;

		/* For MVC we output both views side by side. */
		const int outputWidth = mvc ? frame.width_Y * 2 : frame.width_Y;

		output_
		    << "YUV4MPEG2 "
		    << "W" << outputWidth
		    << " H" << frame.height_Y
		    << " F" << fnum << ":" << fden
		    << " Ip A1:1 C420mpeg2\n";

		if (!output_)
		{
			return false;
		}

		y4mStarted_ = true;
	}

	// Frame header
	output_ << "FRAME\n";

	if (!output_)
	{
		return false;
	}

	// Luma
	for (int y = 0; y < frame.height_Y; y++)
	{
		const uint8_t* left = frame.samples[0] + static_cast<size_t>(y) * static_cast<size_t>(frame.stride_Y);
		output_.write(reinterpret_cast<const char*>(left), static_cast<std::streamsize>(frame.width_Y));

		if (mvc)
		{
			const uint8_t* right = frame.samples_mvc[0] + static_cast<size_t>(y) * static_cast<size_t>(frame.stride_Y);
			output_.write(reinterpret_cast<const char*>(right), static_cast<std::streamsize>(frame.width_Y));
		}

		if (!output_)
		{
			return false;
		}
	}

	// Cb
	for (int y = 0; y < frame.height_C; y++)
	{
		const uint8_t* left = frame.samples[1] + static_cast<size_t>(y) * static_cast<size_t>(frame.stride_C);
		output_.write(reinterpret_cast<const char*>(left), static_cast<std::streamsize>(frame.width_C));

		if (mvc)
		{
			const uint8_t* right = frame.samples_mvc[1] + static_cast<size_t>(y) * static_cast<size_t>(frame.stride_C);
			output_.write(reinterpret_cast<const char*>(right), static_cast<std::streamsize>(frame.width_C));
		}

		if (!output_)
		{
			return false;
		}
	}

	// Cr
	for (int y = 0; y < frame.height_C; y++)
	{
		const uint8_t* left = frame.samples[2] + static_cast<size_t>(y) * static_cast<size_t>(frame.stride_C);
		output_.write(reinterpret_cast<const char*>(left), static_cast<std::streamsize>(frame.width_C));

		if (mvc)
		{
			const uint8_t* right = frame.samples_mvc[2] + static_cast<size_t>(y) * static_cast<size_t>(frame.stride_C);
			output_.write(reinterpret_cast<const char*>(right), static_cast<std::streamsize>(frame.width_C));
		}

		if (!output_)
		{
			return false;
		}
	}

	return true;
}

/** receive decoded frames from edge264.
 * @return success status of processing received frames.
 */
bool MVC2SBSConverter::drainFrames(void)
{
	Edge264Frame frame;

	while (edge264_get_frame(decoder_, &frame, 0) == 0)
	{
		switch (m_format)
		{
			case FORMAT_YUV4MPEG2:

				if (!writeFrameYUV4MPEG2(frame))
				{
					return false;
				}
				break;
			case FORMAT_H264:

				if (!writeFrameH264(frame))
				{
					return false;
				}
				break;
			default:
				break;
		}
	}

	return true;
}

/** decode NAL found in input buffer.
 * @param begin start index of NAL in input buffer.
 * @param end end index of NAL in input buffer.
 * @return flag indicating success or failure of decoding.
 */
bool MVC2SBSConverter::decodeNAL(const size_t begin, const size_t end)
{
	if (begin >= end)
	{
		return false;
	}

	const uint8_t* nal = input_.data() + begin;
	const uint8_t* nalEnd = input_.data() + end;
	unsigned stuck = 0;

	while (true)
	{
		const int res = edge264_decode_NAL(decoder_, nal, nalEnd, nullptr, nullptr);
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
			// A corrupt NAL should not stop the complete stream.
			//
			// The NAL itself has already been passed to edge264 and will
			// not be retried. Continue with the next NAL.
			skippedCorrupt_++;

			if (skippedCorrupt_ <= 32)
			{
				// std::cerr << "edge264: skipped corrupt NAL unit" << std::endl;
			}

			return true;
		}

		// Any other edge264 error is fatal.
		return false;
	}
}

/** process buffered Annex-B input in input buffer.
 * Ignores all data before the first start code.
 * Decode complete NAL, if two start codes have been found.
 * @param endOfStream flag for the end of stream,
 * indicating no more data to be received.
 * @return flag indicating that all data has been processed.
 */
bool MVC2SBSConverter::processInput(const bool endOfStream)
{
	while (true)
	{
		const size_t size = input_.size();

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
					if (input_[i] != 0)
					{
						return false;
					}
				}

				input_.clear();
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
					if (input_[i] != 0)
					{
						return false;
					}
				}

				input_.erase(input_.begin(), input_.begin() + static_cast<std::ptrdiff_t>(size - 3));
			}

			return true;
		}

		// Only zero bytes may occur before the first start code.
		for (size_t i = 0; i < start; ++i)
		{
			if (input_[i] != 0)
			{
				return false;
			}
		}

		// Determine start-code length.
		const size_t startCodeSize = ((start + 2 < size) && (input_[start + 2] == 1)) ? 3 : 4;
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
				input_.erase(input_.begin(), input_.begin() + static_cast<std::ptrdiff_t>(start));
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
					input_.erase(input_.begin(), input_.begin() + static_cast<std::ptrdiff_t>(start));
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

			input_.clear();
			consumed_ = 0;

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
		input_.erase(input_.begin(), input_.begin() + static_cast<std::ptrdiff_t>(nextStart));

		consumed_ = 0;
	}
}

/** put a packet of data into input stream.
 * @param packet input data packet.
 * @return flag for successful insertion.
 */
bool MVC2SBSConverter::feed(const std::vector<uint8_t>& packet)
{
	if (finished_)
	{
		return false;
	}

	if (packet.empty())
	{
		return true;
	}

	// Avoid an unreasonable amount of buffered data.
	if (input_.size() > MAX_NAL_SIZE + 4 - packet.size())
	{
		return false;
	}

	input_.insert(input_.end(), packet.begin(), packet.end());

	return processInput(false);
}

/** fetch remaining frames.
 * @return success status of flush operation.
 */
bool MVC2SBSConverter::flush_yuv4mpeg2(void)
{
	/* Force edge264 to expose frames which are still delayed internally. */
	uint8_t dummy = 0;
	const uint8_t* begin = &dummy;
	const uint8_t* end   = &dummy;

	for (unsigned i = 0; i < 256; i++)
	{
		const int res = edge264_decode_NAL(decoder_, begin, end, nullptr, nullptr);

		if (!drainFrames())
		{
			return false;
		}

		if ((res == 0) ||
		    (res == ENOBUFS) ||
		    (res == ENOTSUP))
		{
			continue;
		}
		else if (res == ENODATA)
		{
			break;
		}
		else if (res == EBADMSG)
		{
			skippedCorrupt_++;
			continue;
		}

		return false;
	}

	edge264_flush(decoder_);

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

		output_.write(reinterpret_cast<const char*>(m_packet_av->data), m_packet_av->size);

		av_packet_unref(m_packet_av);
	}
	return true;
}

/** finish decoding and consume all remaining frames.
 * @return success status of decoding the last frames.
 */
bool MVC2SBSConverter::finish(void)
{
	if (finished_)
	{
		return true;
	}

	if (!processInput(true))
	{
		return false;
	}

	if (((m_format == FORMAT_YUV4MPEG2) && (!flush_yuv4mpeg2())) ||
	    ((m_format == FORMAT_H264) && (!flush_h264())))
	{
		return false;
	}

	finished_ = true;

	if (!input_.empty())
	{
		return false;
	}

	if (skippedCorrupt_ != 0)
	{
		// optional stderr logging
	}

	return output_.good();
}
