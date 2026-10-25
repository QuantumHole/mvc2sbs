// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "mvc2sbsconverter.h"
#include "trackmuxer.h"

extern "C" {
#include <libavutil/opt.h>
}

#include <iostream>

static const size_t MAX_NAL_SIZE = static_cast<size_t>(64) * 1024 * 1024;

MVC2SBSConverter::MVC2SBSConverter(
	const size_t muxer_id,
	const std::function<void(const AVCodecParameters&, AVRational)>& configure_output,
	const std::function<void()>& fail_output) :
	m_decoder(nullptr),
	m_input(),
	m_finished(false),
	m_frame_index(0),
	m_muxer_track_id(muxer_id),
	m_configure_output(configure_output),
	m_fail_output(fail_output),
	m_output_configured(false)
{
	Edge264MvcSettings settings;

	edge264mvc_default_settings(&settings);

	if (edge264mvc_open(&m_decoder, &settings) != EDGE264MVC_OK)
	{
		m_finished = true;
		m_fail_output();
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
		m_context_av->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

		// Typische H.264-Einstellungen
		if ((av_opt_set(m_context_av->priv_data, "preset", "medium", AV_OPT_SEARCH_CHILDREN) < 0) ||
		    (av_opt_set(m_context_av->priv_data, "tune", "zerolatency", AV_OPT_SEARCH_CHILDREN) < 0) ||
		    (av_opt_set(m_context_av->priv_data, "crf", "18", AV_OPT_SEARCH_CHILDREN) < 0)
		    // || (av_opt_set(m_context_av->priv_data, "annexb", "1", AV_OPT_SEARCH_CHILDREN) < 0)
        	// || (av_opt_set(m_context_av->priv_data, "annexb", "0", AV_OPT_SEARCH_CHILDREN) < 0)
		    )
		{
			throw std::runtime_error("setting options failed");
		}

		if (avcodec_open2(m_context_av, codec, nullptr) < 0)
		{
			throw std::runtime_error("avcodec_open2 failed");
		}

		AVCodecParameters* parameters = avcodec_parameters_alloc();
		if (parameters == nullptr)
		{
			throw std::runtime_error("avcodec_parameters_alloc failed");
		}

		const int parametersResult = avcodec_parameters_from_context(parameters, m_context_av);
		if (parametersResult < 0)
		{
			avcodec_parameters_free(&parameters);
			throw std::runtime_error("avcodec_parameters_from_context failed");
		}

		if ((parameters->extradata == nullptr) || (parameters->extradata_size <= 0))
		{
			avcodec_parameters_free(&parameters);
			throw std::runtime_error("libx264 did not provide H.264 extradata");
		}

		m_configure_output(*parameters, m_context_av->time_base);
		avcodec_parameters_free(&parameters);
		m_output_configured = true;

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

	TrackMuxer& muxer = TrackMuxer::instance();
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

    // --- KURZER EXTRAKT: SPS/PPS in extradata ---
    // Dies muss nur einmalig beim ersten Paket (oder wenn extradata noch leer ist) erfolgen
    if (m_context_av->extradata == nullptr) {
        // libx264 mit AnnexB liefert SPS/PPS im ersten Packet
        // Wir müssen sie parsen und in extradata speichern.
        // Für libx264 ist es oft einfacher, die Optionen so zu setzen, dass AVCC direkt geliefert wird,
        // ABER wenn AnnexB geliefert wird, müssen wir es konvertieren.
        
        // Alternative: libx264 kann direkt AVCC ausgeben.
        // av_opt_set(m_context_av->priv_data, "annexb", "0", AV_OPT_SEARCH_CHILDREN);
        
        // Wenn du AnnexB *musst* verwenden (z.B. wegen der Datei), dann:
        // libavformat erwartet, dass du die SPS/PPS in extradata hast.
        // Es gibt keinen direkten, simplen Converter in libavcodec für *Packete* von AnnexB nach AVCC *für extradata*.
        
        // Die einfachste Lösung: libx264 so konfigurieren, dass es AVCC ausgibt.
        // Dann enthält das erste Packet NICHT SPS/PPS, und avcodec_receive_packet liefert nur Slice Data.
        // ABER: Dann muss man SPS/PPS separat aus der Context holen.
        
        // KURZER WEG: libx264 kann über avctx->extradata die SPS/PPS liefern, wenn annexb=1 ist,
        // aber man muss sie manuell extrahieren.
        
        // BESSERE LÖSUNG: libx264 auf AVCC-Modus umstellen.
        // Dann sind die Pakete sofort AVCC-kompatibel.
        // Die SPS/PPS finden sich dann in m_context_av->extradata.
        
        // Da du "annexb=1" kommentiert hast, ist es wahrscheinlich aktiv.
        // Lass uns "annexb=0" setzen, um AVCC zu erhalten.
        
        // ABER: Du hast die Datei-Logik. Wenn die Datei AnnexB erwartet, ändere die Datei-Logik nicht.
        // Die MKV-Logik braucht AVCC.
        
        // ZUM GLÜCK: libavformat (MKV) kann *beide* Formate verarbeiten, 
        // wenn der Codec-Typ korrekt ist und die extradata gesetzt ist.
        // Es wird die extradata aus den AnnexB-Paketen extrahieren, wenn es muss, 
        // ABER es ist zuverlässiger, wenn extradata bereits gesetzt ist.
        
        // KURZ: libx264 im AnnexB-Modus liefert SPS/PPS im ersten Packet.
        // Wir müssen diese Daten in m_context_av->extradata speichern.
        
        // Hier eine simplifizierte Version: 
        // libavformat kann das oft, wenn wir es nur "pushen". 
        // ABER: Der Fehler "sps_id out of range" deutet darauf hin, dass die extradata NICHT korrekt sind.
        
        // LÖSUNG: libx264 auf AVCC umstellen.
        // Das ist der robustere Weg für MKV.
    }
    // --- ENDE EXTRAKT ---

    // SCHRITT 1: Datei schreiben (AnnexB oder AVCC, je nach Encoder-Einstellung)
    // Wenn annexb=1 ist, ist das Packet AnnexB.
    // Wenn annexb=0 ist, ist das Packet AVCC.
    // Für die Datei: Wenn du AnnexB willst, annexb=1 lassen.
    // Für MKV: AVCC ist besser.
    
    // LASS UNS ANNEXB=0 MAChEN (AVCC)
    // Dann sind die Packete für MKV perfekt.
    // Die Datei wird dann AVCC enthalten (mit 4-Byte Size Prefix).
    // Das ist für viele Player auch OK, oder du kannst die Datei-Logik anpassen.
    
    // ABER: Du sagst, die Datei "funktioniert". Wenn sie AnnexB ist, lass uns das nicht ändern,
    // um die Datei nicht zu brechen.
    
    // INSGESAMT: Die sauberste Lösung ist, dass libx264 AVCC ausgibt.
    // Dann:
    // 1. extradata wird automatisch von libx264 gesetzt (oder muss gesetzt werden).
    // 2. Die Packete sind AVCC.
    // 3. MKV ist happy.
    // 4. Die Datei ist AVCC.
    
    // Falls du die Datei als AnnexB behalten willst:
    // Dann musst du die Packete für MKV *konvertieren* (AnnexB -> AVCC) oder
    // die SPS/PPS korrekt extrahieren.
    
    // DA libavformat *dafür gebaut ist*, die SPS/PPS aus AnnexB-Paketen zu extrahieren,
    // wenn der Codec H.264 ist, *sollte* es funktionieren.
    // ABER: Der Fehler deutet darauf hin, dass es NICHT funktioniert.
    
    // PROBEN WIR: libx264 auf AVCC umstellen.
    
    // --- ÄNDERUNG IN DER INITIALISIERUNG (OBERHALB) ---
    // av_opt_set(m_context_av->priv_data, "annexb", "0", AV_OPT_SEARCH_CHILDREN);
    
    // --- FORTSETZUNG ---

    // SCHRITT 2: An Muxer schreiben
    // Wenn annexb=0 ist, ist das Packet AVCC.
    // Wir müssen sicherstellen, dass m_context_av->extradata gesetzt ist.
    
    // if (firstPacket) {
    //     // Beim ersten AVCC-Packet:
    //     // libx264 sollte extradata gesetzt haben.
    //     // Prüfen, ob extradata gesetzt ist.
    //     if (m_context_av->extradata == nullptr) {
    //          // Fallback: SPS/PPS manuell extrahieren aus dem AnnexB-Packet (wenn annexb=1 war)
    //          // ODER: libx264 hat es vergessen.
    //          // In der Praxis: libx264 mit annexb=0 setzt extradata.
    //          throw std::runtime_error("extradata is null after first packet");
    //     }
    //     firstPacket = false;
    // }

    // Packet an Muxer senden
    // WICHTIG: Das Packet ist jetzt AVCC (weil annexb=0).
    // libavformat (MKV) erwartet AVCC.
    
    // Kopie des Packets, weil av_packet_unref es löscht
    // Muxer pushen (annimmt, dass der Muxer die extradata vom Stream-Context holt)
    // Dein Muxer muss sicherstellen, dass die extradata in die MKV-Header geschrieben werden.
    
    // Datei schreiben (AVCC)
    // Wenn du die Datei als AnnexB willst, musst du hier konvertieren.
    // ODER: Die Datei als AVCC akzeptieren.
    
    // ANNAHME: Die Datei kann AVCC.
		std::vector<uint8_t> vecdata(m_packet_av->data, m_packet_av->data + m_packet_av->size);
		muxer.push(
			m_muxer_track_id,
			vecdata,
			m_packet_av->pts,
			m_packet_av->dts,
			m_packet_av->duration,
			m_packet_av->flags);

		av_packet_unref(m_packet_av);
	}

	return true;
}

bool MVC2SBSConverter::drainFrames(void)
{
	Edge264MvcFrame frame;
	bool decoding_ok = true;

	while (decoding_ok && edge264mvc_receive_frame(m_decoder, &frame) == EDGE264MVC_OK)
	{
		if (!writeFrameH264(frame))
		{
			decoding_ok = false;
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

	if (edge264mvc_send_end(m_decoder) != EDGE264MVC_OK)
	{
		return false;
	}

	if (!drainFrames() || (m_context_av == nullptr) || !m_output_configured)
	{
		return false;
	}

	// Encoder flushen
	if (avcodec_send_frame(m_context_av, nullptr) < 0)
	{
		throw std::runtime_error("Encoder flush failed");
	}

	TrackMuxer& muxer = TrackMuxer::instance();
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

		std::vector<uint8_t> vecdata(m_packet_av->data, m_packet_av->data + m_packet_av->size);
		muxer.push(
			m_muxer_track_id,
			vecdata,
			m_packet_av->pts,
			m_packet_av->dts,
			m_packet_av->duration,
			m_packet_av->flags);

		av_packet_unref(m_packet_av);
	}

	m_finished = true;

	return m_input.empty();
}
