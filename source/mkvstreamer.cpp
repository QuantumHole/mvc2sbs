// SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "mkvstreamer.h"
#include "pgsconverter.h"
#include "h264converter.h"
#include "copyconverter.h"

#include <ebml/EbmlHead.h>
#include <ebml/StdIOCallback.h>

#include <matroska/KaxContexts.h>
#include <matroska/KaxCluster.h>
#include <matroska/KaxSemantic.h>
#include <matroska/KaxSegment.h>

#include <cmath>
#include <iostream>

// libebml      1.4.7
// libmatroska  1.7.2

static bool isElement(const libebml::EbmlElement& element, const libebml::EbmlId& id)
{
	return element.Generic().ClassId() == id;
}

template<typename T>
const T* FindElement(const libebml::EbmlMaster& master)
{
	return static_cast<const T*>(master.FindFirstElt(EBML_INFO(T)));
}

static uint64_t GetTrackNumber(const libmatroska::KaxTrackEntry* track)
{
	if (!track)
	{
		return 0;
	}

	const libmatroska::KaxTrackNumber* element = FindElement<libmatroska::KaxTrackNumber>(*track);

	return element
	    ? static_cast<uint64_t>(*element)
	    : 0;
}

inline std::string GetCodecId(const libmatroska::KaxTrackEntry* track)
{
	if (!track)
	{
		return {};
	}

	const libmatroska::KaxCodecID* element = FindElement<libmatroska::KaxCodecID>(*track);

	if (!element)
	{
		return {};
	}

	return static_cast<std::string>(*element);
}

static uint64_t readTimecodeScale(
	libmatroska::KaxInfo& info,
	libebml::EbmlStream& stream)
{
	int upperLevel = 0;
	libebml::EbmlElement* foundElement = nullptr;

	info.Read(
		stream,
		info.Generic().Context,
		upperLevel,
		foundElement,
		false,
		libebml::SCOPE_ALL_DATA);

	delete foundElement;

	const libebml::EbmlMaster* master = dynamic_cast<const libebml::EbmlMaster*>(&info);

	if (master == nullptr)
	{
		return 1000000ULL;
	}

	for (const libebml::EbmlElement* child : *master)
	{
		if ((child == nullptr) ||
		    !isElement(*child, EBML_ID(libmatroska::KaxTimecodeScale)))
		{
			continue;
		}

		const libmatroska::KaxTimecodeScale* scale = dynamic_cast<const libmatroska::KaxTimecodeScale*>(child);

		if (scale != nullptr)
		{
			return scale->GetValue();
		}
	}

	return 1000000ULL;
}

static std::vector<uint8_t> GetCodecPrivate(const libmatroska::KaxTrackEntry* track)
{
	if (!track)
	{
		return {};
	}

	const libmatroska::KaxCodecPrivate* element = FindElement<libmatroska::KaxCodecPrivate>(*track);

	if (!element)
	{
		return {};
	}

	const uint8_t* buffer = element->GetBuffer();
	const size_t size = element->GetSize();

	if (!buffer || (size == 0))
	{
		return {};
	}

	return {
	    reinterpret_cast<const uint8_t*>(buffer),
	    reinterpret_cast<const uint8_t*>(buffer) + size
	};
}

static uint64_t GetVideoWidth(const libmatroska::KaxTrackEntry* track)
{
	if (!track)
	{
		return 0;
	}

	const libmatroska::KaxTrackVideo* video = FindElement<libmatroska::KaxTrackVideo>(*track);

	if (!video)
	{
		return 0;
	}

	const libmatroska::KaxVideoPixelWidth* width = FindElement<libmatroska::KaxVideoPixelWidth>(*video);

	return width
	    ? static_cast<uint64_t>(*width)
	    : 0;
}

static uint64_t GetVideoHeight( const libmatroska::KaxTrackEntry* track)
{
	if (!track)
	{
		return 0;
	}

	const libmatroska::KaxTrackVideo* video = FindElement<libmatroska::KaxTrackVideo>(*track);

	if (!video)
	{
		return 0;
	}

	const libmatroska::KaxVideoPixelHeight* height = FindElement<libmatroska::KaxVideoPixelHeight>(*video);

	return height
	    ? static_cast<uint64_t>(*height)
	    : 0;
}

static double GetAudioSampleRate(const libmatroska::KaxTrackEntry* track)
{
	if (!track)
	{
		return 8000.0;
	}

	const libmatroska::KaxTrackAudio* audio = FindElement<libmatroska::KaxTrackAudio>(*track);

	if (!audio)
	{
		return 8000.0;
	}

	const libmatroska::KaxAudioSamplingFreq* sampleRate =
		FindElement<libmatroska::KaxAudioSamplingFreq>(*audio);
	return sampleRate ? sampleRate->GetValue() : 8000.0;
}

static uint64_t GetAudioChannels(const libmatroska::KaxTrackEntry* track)
{
	if (!track)
	{
		return 1;
	}

	const libmatroska::KaxTrackAudio* audio = FindElement<libmatroska::KaxTrackAudio>(*track);

	if (!audio)
	{
		return 1;
	}

	const libmatroska::KaxAudioChannels* channels = FindElement<libmatroska::KaxAudioChannels>(*audio);
	return channels ? channels->GetValue() : 1;
}

static uint64_t GetCodecDelay(const libmatroska::KaxTrackEntry* track)
{
	if (!track)
	{
		return 0;
	}

	const libmatroska::KaxCodecDelay* element = FindElement<libmatroska::KaxCodecDelay>(*track);

	return element
	    ? static_cast<uint64_t>(*element)
	    : 0;
}

static uint64_t GetDefaultDuration(const libmatroska::KaxTrackEntry* track)
{
	if (!track)
	{
		return 0;
	}

	const libmatroska::KaxTrackDefaultDuration* element = FindElement<libmatroska::KaxTrackDefaultDuration>(*track);

	return element
	    ? static_cast<uint64_t>(*element)
	    : 0;
}

static uint64_t GetGlobalTimecodeScale(const libmatroska::KaxTrackEntry* track)
{
	constexpr uint64_t defaultScale = 1'000'000; // nanoseconds

	if (!track)
	{
		return defaultScale;
	}

	const libmatroska::KaxTimecodeScale* element = FindElement<libmatroska::KaxTimecodeScale>(*track);

	return element
	    ? element->GetValue()
	    : defaultScale;
}

static std::string GetTrackLanguage(const libmatroska::KaxTrackEntry* track)
{
	if (!track)
	{
		return "und";
	}

	const libmatroska::KaxTrackLanguage* element = FindElement<libmatroska::KaxTrackLanguage>(*track);

	if (!element)
	{
		return "und";
	}

	const std::string language = static_cast<std::string>(*element);

	return language.empty() ? "und" : language;
}

static std::string GetTrackTitle(const libmatroska::KaxTrackEntry* track)
{
	if (!track)
	{
		return "";
	}

	const libmatroska::KaxTrackName* element = FindElement<libmatroska::KaxTrackName>(*track);

	if (!element)
	{
		return "";
	}

	const std::string title = element->GetValueUTF8();
	return title;
}

static uint64_t getClusterEnd(
	const libebml::EbmlElement& cluster,
	const uint64_t dataStart,
	const size_t fileSize)
{
	if (!cluster.IsFiniteSize())
	{
		return static_cast<uint64_t>(fileSize);
	}

	const uint64_t size = cluster.GetSize();
	const uint64_t end = dataStart + size;

	return end > static_cast<uint64_t>(fileSize)
	    ? static_cast<uint64_t>(fileSize)
	    : end;
}

static TrackConverter::TrackInfo GetH264TrackInfo(const libmatroska::KaxTrackEntry* track)
{
	TrackConverter::TrackInfo result;

	if (!track)
	{
		return result;
	}

	result.trackNumber = GetTrackNumber(track);
	result.codecId = GetCodecId(track);
	result.codecPrivate = GetCodecPrivate(track);
	result.width = GetVideoWidth(track);
	result.height = GetVideoHeight(track);
	result.sampleRate = GetAudioSampleRate(track);
	result.channels = GetAudioChannels(track);
	result.codecDelay = GetCodecDelay(track);
	result.defaultDuration = GetDefaultDuration(track);
	result.timestampScale = GetGlobalTimecodeScale(track);
	result.language = GetTrackLanguage(track);
	result.title = GetTrackTitle(track);

	return result;
}

static uint64_t getFramePts(
	const uint64_t blockPts,
	const unsigned int frameNumber,
	const TrackConverter::TrackInfo& trackInfo)
{
	if (frameNumber == 0)
	{
		return blockPts;
	}

	if (trackInfo.defaultDuration == 0)
	{
		/*
		 * Ohne bekannte Dauer können wir den PTS
		 * weiterer gelaceter Frames hier nicht
		 * zuverlässig bestimmen.
		 */
		return blockPts;
	}

	return blockPts +
	       static_cast<uint64_t>(frameNumber) *
	       trackInfo.defaultDuration;
}

MKVStreamer::MKVStreamer(const std::string& filename) :
	m_input_file_name(filename),
	m_input_file_size(0),
	m_converters(),
	m_last_status_time(std::chrono::steady_clock::now()),
	m_muxer(TrackMuxer::instance(output_file_name(filename))),
	m_thread_mux()
{
}

MKVStreamer::~MKVStreamer(void)
{
	if (m_thread_mux.joinable())
	{
		m_thread_mux.join();
	}
}

std::string MKVStreamer::output_file_name(const std::string& input_file_name) const
{
	const size_t slash = input_file_name.find_last_of("/\\");
	const size_t start = (slash == std::string::npos) ? 0 : (slash + 1);
	const size_t dot = input_file_name.find_last_of('.');
	const size_t ext = ((dot != std::string::npos) && (dot >= start)) ? dot : input_file_name.size();
	const std::string directory = (slash == std::string::npos) ? std::string() : input_file_name.substr(0, slash + 1);

	return directory +
	       input_file_name.substr(start, ext - start) +
	       ".sbs" +
	       input_file_name.substr(ext);
}

void MKVStreamer::readBlock(
	libmatroska::KaxInternalBlock& block,
	libebml::EbmlStream& stream,
	const uint64_t clusterTimecode,
	const uint64_t timecodeScale)
{
	block.ReadData(stream.I_O(), libebml::SCOPE_ALL_DATA);

	const uint64_t trackNumber = block.TrackNum();
	const std::map<uint64_t, TrackConverter*>::iterator convIt = m_converters.find(trackNumber);

	if (convIt == m_converters.end())
	{
		return;
	}

	TrackConverter& converter = *convIt->second;

	/*
	 * GetRelativeTimestamp() liefert den unskalierten
	 * relativen Block-Timecode.
	 */
	const int64_t relativeTimecode = block.GetRelativeTimestamp();
	const int64_t absoluteTimecode = static_cast<int64_t>(clusterTimecode) + relativeTimecode;

	if ((absoluteTimecode <= 0) || (timecodeScale == 0))
	{
		return;
	}

	const uint64_t blockPts = static_cast<uint64_t>(absoluteTimecode) * timecodeScale;

	bool keyframe = false;

	if (libmatroska::KaxSimpleBlock* simpleBlock = dynamic_cast<libmatroska::KaxSimpleBlock*>(&block))
	{
		keyframe = simpleBlock->IsKeyframe();
	}

	for (unsigned int frameNumber = 0; frameNumber < block.NumberFrames(); frameNumber++)
	{
		libmatroska::DataBuffer& data = block.GetBuffer(frameNumber);

		TrackConverter::FrameInfo frameinfo;
		frameinfo.index = frameNumber;
		frameinfo.pts = getFramePts(blockPts, frameNumber, converter.track_info());
		frameinfo.duration = converter.track_info().defaultDuration;
		frameinfo.keyframe = keyframe;

		converter.add_data(data.Buffer(), data.Size(), frameinfo);
	}

	/* print status only once per second. */
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

	if (now - m_last_status_time < std::chrono::seconds(1))
	{
		return;
	}
	m_last_status_time = now;

	const double seconds = static_cast<double>(blockPts) / 1e9;
	std::cout << "queued timecode: " << seconds << "s.          \r";
	fflush(stdout);
}

void MKVStreamer::streamBlockGroup(
	libebml::EbmlStream& stream,
	const uint64_t groupEnd,
	const uint64_t clusterTimecode,
	const uint64_t timecodeScale)
{
	int upperLevel = 0;

	while (stream.I_O().getFilePointer() < groupEnd)
	{
		libebml::EbmlElement* element =
			stream.FindNextElement(
				libmatroska::KaxBlockGroup_Context,
				upperLevel,
				m_input_file_size,
				false);

		if (element == nullptr)
		{
			return;
		}

		if (element->GetElementPosition() >= groupEnd)
		{
			delete element;
			return;
		}

		libmatroska::KaxInternalBlock* block = dynamic_cast<libmatroska::KaxInternalBlock*>(element);

		if (block != nullptr)
		{
			const uint64_t dataStart = stream.I_O().getFilePointer();
			const uint64_t blockSize = block->GetSize();

			if (block->IsFiniteSize() &&
			    ((dataStart > groupEnd) ||
			     (blockSize > groupEnd - dataStart)))
			{
				delete element;

				stream.I_O().setFilePointer(
					static_cast<int64_t>(groupEnd),
					libebml::seek_beginning);

				return;
			}

			try
			{
				readBlock(*block, stream, clusterTimecode, timecodeScale);
			}
			catch (...)
			{
				delete element;

				stream.I_O().setFilePointer(
					static_cast<int64_t>(groupEnd),
					libebml::seek_beginning);

				return;
			}

			delete element;
			continue;
		}

		try
		{
			element->SkipData(stream, element->Generic().Context);
		}
		catch (...)
		{
			delete element;

			stream.I_O().setFilePointer(
				static_cast<int64_t>(groupEnd),
				libebml::seek_beginning);

			return;
		}

		delete element;
	}
}

void MKVStreamer::streamCluster(
	libebml::EbmlStream& stream,
	const uint64_t clusterEnd,
	const uint64_t timecodeScale)
{
	int upperLevel = 0;
	uint64_t clusterTimecode = 0;
	bool haveClusterTimecode = false;

	while (stream.I_O().getFilePointer() < clusterEnd)
	{
		libebml::EbmlElement* element =
			stream.FindNextElement(
				libmatroska::KaxCluster_Context,
				upperLevel,
				m_input_file_size,
				false);

		if (element == nullptr)
		{
			stream.I_O().setFilePointer(static_cast<int64_t>(clusterEnd), libebml::seek_beginning);
			return;
		}

		if (element->GetElementPosition() >= clusterEnd)
		{
			delete element;
			break;
		}

		if (isElement(*element, EBML_ID(libmatroska::KaxClusterTimecode)))
		{
			libmatroska::KaxClusterTimecode* timecode = dynamic_cast<libmatroska::KaxClusterTimecode*>(element);

			if (timecode != nullptr)
			{
				timecode->ReadData(stream.I_O(), libebml::SCOPE_ALL_DATA);
				clusterTimecode = timecode->GetValue();
				haveClusterTimecode = true;
			}

			delete element;
			continue;
		}

		libmatroska::KaxInternalBlock* block = dynamic_cast<libmatroska::KaxInternalBlock*>(element);

		if (block != nullptr)
		{
			const uint64_t dataStart = stream.I_O().getFilePointer();
			const uint64_t blockSize = block->GetSize();

			if (block->IsFiniteSize() &&
			    ((dataStart > clusterEnd) ||
			     (blockSize > clusterEnd - dataStart)))
			{
				delete element;

				stream.I_O().setFilePointer(
					static_cast<int64_t>(clusterEnd),
					libebml::seek_beginning);

				return;
			}

			try
			{
				if (haveClusterTimecode)
				{
					readBlock(*block, stream, clusterTimecode, timecodeScale);
				}
				else
				{
					block->ReadData(stream.I_O(), libebml::SCOPE_ALL_DATA);
				}
			}
			catch (...)
			{
				delete element;

				stream.I_O().setFilePointer(
					static_cast<int64_t>(clusterEnd),
					libebml::seek_beginning);

				return;
			}

			delete element;
			continue;
		}

		if (isElement(*element, EBML_ID(libmatroska::KaxBlockGroup)))
		{
			const uint64_t groupDataStart = stream.I_O().getFilePointer();
			const uint64_t groupSize = element->GetSize();
			uint64_t groupEnd = clusterEnd;

			if (element->IsFiniteSize())
			{
				if ((groupDataStart > clusterEnd) ||
				    (groupSize > clusterEnd - groupDataStart))
				{
					delete element;

					stream.I_O().setFilePointer(
						static_cast<int64_t>(clusterEnd),
						libebml::seek_beginning);

					return;
				}

				groupEnd = groupDataStart + groupSize;
			}

			streamBlockGroup(stream, groupEnd, clusterTimecode, timecodeScale);

			delete element;
			continue;
		}

		const uint64_t dataStart = stream.I_O().getFilePointer();

		if (element->IsFiniteSize())
		{
			const uint64_t size = element->GetSize();

			if ((dataStart > clusterEnd) ||
			    (size > clusterEnd - dataStart))
			{
				delete element;

				stream.I_O().setFilePointer(
					static_cast<int64_t>(clusterEnd),
					libebml::seek_beginning);

				return;
			}
		}

		try
		{
			element->SkipData(stream, element->Generic().Context);
		}
		catch (...)
		{
			delete element;

			stream.I_O().setFilePointer(
				static_cast<int64_t>(clusterEnd),
				libebml::seek_beginning);

			return;
		}

		delete element;
	}
}

void MKVStreamer::readTracks(libmatroska::KaxTracks& tracksElement, libebml::EbmlStream& stream)
{
	int upperLevel = 0;
	libebml::EbmlElement* foundElement = nullptr;

	tracksElement.Read(
		stream,
		tracksElement.Generic().Context,
		upperLevel,
		foundElement,
		false,
		libebml::SCOPE_ALL_DATA);

	delete foundElement;

	const libebml::EbmlMaster* master = dynamic_cast<const libebml::EbmlMaster*>(&tracksElement);

	if (master == nullptr)
	{
		return;
	}

	for (const libebml::EbmlElement* child : *master)
	{
		if ((child == nullptr) ||
		    !isElement(*child, EBML_ID(libmatroska::KaxTrackEntry)))
		{
			continue;
		}

		const libmatroska::KaxTrackEntry* entry = dynamic_cast<const libmatroska::KaxTrackEntry*>(child);

		if (entry == nullptr)
		{
			continue;
		}

		TrackConverter::TrackInfo info = GetH264TrackInfo(entry);

		if (info.codecId == "S_HDMV/PGS")
		{
			m_converters[info.trackNumber] = new PGSConverter(m_input_file_name, info);
		}
		else if (info.codecId == "V_MPEG4/ISO/AVC")
		{
			m_converters[info.trackNumber] = new H264Converter(info);
		}
		else
		{
			// copy all other tracks
			m_converters[info.trackNumber] = new CopyConverter(info);
			// m_muxer.add_file(m_input_file_name, info.trackNumber, "matroska", "", "");
		}
	}

	// all tracks/converters have been defined.
	// muxer may now wait for packets/frames.
	m_thread_mux = std::thread(&TrackMuxer::mux, &m_muxer);
}

void MKVStreamer::streamSegment(libebml::EbmlStream& stream, const size_t fileSize)
{
	int upperLevel = 0;
	uint64_t timecodeScale = 1000000ULL;

	while (true)
	{
		libebml::EbmlElement* element =
			stream.FindNextElement(libmatroska::KaxSegment_Context, upperLevel, fileSize, false);

		if (element == nullptr)
		{
			return;
		}

		if (isElement(*element, EBML_ID(libmatroska::KaxInfo)))
		{
			libmatroska::KaxInfo* info = dynamic_cast<libmatroska::KaxInfo*>(element);

			if (info != nullptr)
			{
				timecodeScale = readTimecodeScale(*info, stream);
			}

			delete element;
			continue;
		}

		if (isElement(*element, EBML_ID(libmatroska::KaxTracks)))
		{
			libmatroska::KaxTracks* tracks = dynamic_cast<libmatroska::KaxTracks*>(element);

			if (tracks != nullptr)
			{
				readTracks(*tracks, stream);
			}

			delete element;
			continue;
		}

		if (isElement(*element, EBML_ID(libmatroska::KaxCluster)))
		{
			const uint64_t dataStart = stream.I_O().getFilePointer();
			const uint64_t clusterEnd = getClusterEnd(*element, dataStart, fileSize);

			streamCluster(stream, clusterEnd, timecodeScale);

			delete element;
			continue;
		}

		try
		{
			element->SkipData(stream, element->Generic().Context);
		}
		catch (...)
		{
			delete element;
			return;
		}

		delete element;
	}
}

void MKVStreamer::run(void)
{
	try
	{
		libebml::StdIOCallback file(m_input_file_name.c_str(), MODE_READ);
		libebml::EbmlStream stream(file);
		file.setFilePointer(0, libebml::seek_end);
		m_input_file_size = file.getFilePointer();
		file.setFilePointer(0, libebml::seek_beginning);

		int upperLevel = 0;
		while (true)
		{
			libebml::EbmlElement* element =
				stream.FindNextElement(libmatroska::KaxMatroska_Context, upperLevel, m_input_file_size, false);

			if (element == nullptr)
			{
				break;
			}

			if (isElement(*element, EBML_ID(libebml::EbmlHead)))
			{
				int headerLevel = 0;
				libebml::EbmlElement* foundElement = nullptr;

				element->Read(
					stream,
					element->Generic().Context,
					headerLevel,
					foundElement,
					false,
					libebml::SCOPE_ALL_DATA);

				delete foundElement;
				delete element;

				upperLevel = 0;
				continue;
			}

			if (isElement(*element, EBML_ID(libmatroska::KaxSegment)))
			{
				streamSegment(stream, m_input_file_size);
				delete element;
				break;
			}

			try
			{
				element->SkipData(stream, element->Generic().Context);
			}
			catch (...)
			{
				delete element;
				break;
			}

			delete element;
			upperLevel = 0;
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << "Fehler: " << e.what() << std::endl;
	}

	/* notify all track converters of EOF. */
	for (std::map<uint64_t, TrackConverter*>::iterator it = m_converters.begin(); it != m_converters.end(); ++it)
	{
		it->second->finish();
		delete it->second;
		it->second = nullptr;
	}

	m_converters.clear();
}
