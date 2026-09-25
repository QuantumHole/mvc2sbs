#include "mkvstreamer.h"
#include "pgsconverter.h"

#include <ebml/EbmlElement.h>
#include <ebml/EbmlHead.h>
#include <ebml/StdIOCallback.h>

#include <matroska/KaxContexts.h>
#include <matroska/KaxCluster.h>
#include <matroska/KaxClusterData.h>
#include <matroska/KaxSegment.h>
#include <matroska/KaxInfo.h>
#include <matroska/KaxInfoData.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

// libebml      1.4.7
// libmatroska  1.7.2

static bool isElement(const libebml::EbmlElement& element, const libebml::EbmlId& id)
{
	return element.Generic().ClassId() == id;
}

static bool getTrackNumber(
	const libmatroska::KaxTrackEntry& trackEntry,
	uint64_t& number)
{
	const libebml::EbmlMaster* master = dynamic_cast<const libebml::EbmlMaster*>(&trackEntry);

	if (master == nullptr)
	{
		return false;
	}

	for (const libebml::EbmlElement* child : *master)
	{
		if ((child == nullptr) ||
		    !isElement(*child, EBML_ID(libmatroska::KaxTrackNumber)))
		{
			continue;
		}

		const libmatroska::KaxTrackNumber* value = dynamic_cast<const libmatroska::KaxTrackNumber*>(child);

		if (value == nullptr)
		{
			return false;
		}

		number = value->GetValue();
		return true;
	}

	return false;
}

static std::string getCodecId(const libmatroska::KaxTrackEntry& trackEntry)
{
	const libebml::EbmlMaster* master = dynamic_cast<const libebml::EbmlMaster*>(&trackEntry);

	if (master == nullptr)
	{
		return {};
	}

	for (const libebml::EbmlElement* child : *master)
	{
		if ((child == nullptr) ||
		    !isElement(*child, EBML_ID(libmatroska::KaxCodecID)))
		{
			continue;
		}

		const libmatroska::KaxCodecID* value = dynamic_cast<const libmatroska::KaxCodecID*>(child);

		if (value != nullptr)
		{
			return value->GetValue();
		}
	}

	return {};
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

MKVStreamer::MKVStreamer(const std::string& filename) :
	m_input_file_name(filename),
	m_input_file_size(0),
	m_converters()
{
}

MKVStreamer::~MKVStreamer(void)
{
	for (std::map<uint64_t, TrackConverter*>::iterator it = m_converters.begin(); it != m_converters.end(); ++it)
	{
		delete it->second;
	}
	m_converters.clear();
}

void MKVStreamer::readBlock(
	libmatroska::KaxInternalBlock& block,
	libebml::EbmlStream& stream,
	const uint64_t clusterTimecode,
	const uint64_t timecodeScale)
{
	block.ReadData(stream.I_O(), libebml::SCOPE_ALL_DATA);

	const uint64_t trackNumber = block.TrackNum();
	const std::map<uint64_t, TrackConverter*>::iterator outputIt = m_converters.find(trackNumber);

	if (outputIt == m_converters.end())
	{
		return;
	}

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
	const uint64_t ns = static_cast<uint64_t>(absoluteTimecode) * timecodeScale;

	for (unsigned int frameNumber = 0; frameNumber < block.NumberFrames(); frameNumber++)
	{
		libmatroska::DataBuffer& data = block.GetBuffer(frameNumber);
		m_converters[trackNumber]->add_data(data.Buffer(), data.Size(), ns);
	}
}

/** process block group and iterate over blocks. */
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

/** processes clusters, iterates over blocks. */
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

/** reads track list.
 * associates tracks to TrackConverters
 * before iterating over Clusters and Blocks.
 */
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

		uint64_t number = 0;

		if (!getTrackNumber(*entry, number))
		{
			continue;
		}

		libmatroska::KaxTrackLanguage* track_lang = static_cast<libmatroska::KaxTrackLanguage*>(
			entry->FindFirstElt(EBML_INFO(libmatroska::KaxTrackLanguage)));

		const std::string language = (track_lang ? track_lang->GetValue() : "und");

		if (getCodecId(*entry) == "S_HDMV/PGS")
		{
			m_converters[number] = new PGSConverter(m_input_file_name, number, "S_HDMV/PGS", language);
		}
	}
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
				stream.FindNextElement(
					libmatroska::KaxMatroska_Context,
					upperLevel,
					m_input_file_size,
					false);

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
		return;
	}
}
