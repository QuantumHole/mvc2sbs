#include "mkvstreamer.h"

#include <ebml/EbmlElement.h>
#include <ebml/EbmlStream.h>
#include <ebml/EbmlHead.h>
#include <ebml/StdIOCallback.h>

#include <matroska/KaxContexts.h>
#include <matroska/KaxBlock.h>
#include <matroska/KaxTracks.h>
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

static std::string makeSupFilename(const std::string& inputFilename, const uint64_t trackNumber)
{
	const size_t slash = inputFilename.find_last_of("/\\");
	const size_t start = (slash == std::string::npos) ? 0 : (slash + 1);
	const size_t dot = inputFilename.find_last_of('.');
	const size_t ext = ((dot != std::string::npos) && (dot >= start)) ? dot : inputFilename.size();
	const std::string directory = (slash == std::string::npos) ? std::string() : inputFilename.substr(0, slash + 1);

	return directory +
	       inputFilename.substr(start, ext - start) +
	       "_track" +
	       std::to_string(trackNumber) +
	       ".sup";
}

static void writeUint32BE(std::ofstream& file, const uint32_t value)
{
	const unsigned char buffer[4] =
	{
		static_cast<unsigned char>((value >> 24) & 0xff),
		static_cast<unsigned char>((value >> 16) & 0xff),
		static_cast<unsigned char>((value >> 8) & 0xff),
		static_cast<unsigned char>(value & 0xff)
	};

	file.write(reinterpret_cast<const char*>(buffer), sizeof(buffer));
}

/*
 * Matroska-Timecode-Ticks -> PGS 90-kHz-PTS.
 *
 * timecodeScale ist Nanosekunden pro Matroska-Tick.
 */
static uint32_t matroskaTimecodeToPgsPts(const int64_t timecode, const uint64_t timecodeScale)
{
	if ((timecode <= 0) || (timecodeScale == 0))
	{
		return 0;
	}

	const uint64_t ticks = static_cast<uint64_t>(timecode);
	const uint64_t ns = ticks * timecodeScale;
	const uint64_t pts = (ns * 90000ULL) / 1000000000ULL;

	return static_cast<uint32_t>(pts & 0xffffffffULL);
}

static bool writePgsSupSegments(
	std::ofstream& output,
	const unsigned char* data,
	const size_t size,
	const uint32_t pts,
	const size_t trackNumber)
{
	if ((data == nullptr) || (size == 0))
	{
		std::cerr << "    Leerer PGS-Frame in Track " << trackNumber << std::endl;
		return false;
	}

	size_t offset = 0;
	bool wroteAnything = false;

	while (offset < size)
	{
		if (size - offset < 3)
		{
			std::cerr << "    Ungültiges PGS-Segment" << std::endl;
			return false;
		}

		const uint8_t segmentType = data[offset];

		const uint16_t segmentLength = static_cast<uint16_t>(
			(static_cast<uint16_t>(data[offset + 1]) << 8) |
			static_cast<uint16_t>(data[offset + 2]));

		const size_t segmentSize = 3 + static_cast<size_t>(segmentLength);

		if (segmentSize > size - offset)
		{
			std::cerr
			    << "    PGS-Segment außerhalb des Buffers: "
			    << "type=0x"
			    << std::hex
			    << static_cast<unsigned int>(segmentType)
			    << std::dec
			    << ", length="
			    << segmentLength
			    << std::endl;

			return false;
		}

		const unsigned char magic[2] =
		{
			0x50,
			0x47
		};

		output.write(
			reinterpret_cast<const char*>(magic),
			sizeof(magic));

		writeUint32BE(output, pts);
		writeUint32BE(output, 0);

		output.write(
			reinterpret_cast<const char*>(data + offset),
			static_cast<std::streamsize>(segmentSize));

		if (!output)
		{
			throw std::runtime_error("Fehler beim Schreiben der SUP-Datei");
		}

		wroteAnything = true;
		offset += segmentSize;
	}

	return wroteAnything;
}

static void extractPgsBlock(
	libmatroska::KaxInternalBlock& block,
	std::map<uint64_t, std::ofstream>& outputs,
	const uint64_t clusterTimecode,
	const uint64_t timecodeScale)
{
	const uint64_t trackNumber = block.TrackNum();

	if (outputs.find(trackNumber) == outputs.end())
	{
		return;
	}

	const std::map<uint64_t, std::ofstream>::iterator outputIt = outputs.find(trackNumber);

	if (outputIt == outputs.end())
	{
		return;
	}

	/*
	 * GetRelativeTimestamp() liefert den unskalierten
	 * relativen Block-Timecode.
	 */
	const int64_t relativeTimecode = block.GetRelativeTimestamp();
	const int64_t absoluteTimecode = static_cast<int64_t>(clusterTimecode) + relativeTimecode;
	const uint32_t pts = matroskaTimecodeToPgsPts(absoluteTimecode, timecodeScale);

	for (unsigned int frameNumber = 0; frameNumber < block.NumberFrames(); frameNumber++)
	{
		libmatroska::DataBuffer& data = block.GetBuffer(frameNumber);

		writePgsSupSegments(
			outputIt->second,
			data.Buffer(),
			data.Size(),
			pts,
			trackNumber);
	}
}

static bool readBlock(
	libmatroska::KaxInternalBlock& block,
	libebml::EbmlStream& stream,
	std::map<uint64_t, std::ofstream>& outputs,
	const uint64_t clusterTimecode,
	const uint64_t timecodeScale)
{
	block.ReadData(stream.I_O(), libebml::SCOPE_ALL_DATA);

	const uint64_t trackNumber = block.TrackNum();

	if (outputs.find(trackNumber) == outputs.end())
	{
		return true;
	}

	extractPgsBlock(
		block,
		outputs,
		clusterTimecode,
		timecodeScale);

	return true;
}

static void readTracks(
	libmatroska::KaxTracks& tracksElement,
	libebml::EbmlStream& stream,
	std::map<uint64_t, std::ofstream>& pgsOutputs,
	const std::string& inputFilename)
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

		if (getCodecId(*entry) != "S_HDMV/PGS")
		{
			continue;
		}

		uint64_t number = 0;

		if (!getTrackNumber(*entry, number))
		{
			continue;
		}

		const std::string filename = makeSupFilename(inputFilename, number);

		std::ofstream output;
		output.open(filename, std::ios::binary | std::ios::out | std::ios::trunc);

		if (!output)
		{
			throw std::runtime_error("Kann Datei nicht öffnen: " + filename);
		}

		std::cout << "Track " << number << " -> " << filename << std::endl;

		pgsOutputs.emplace(number, std::move(output));
	}
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

static void streamBlockGroup(
	libebml::EbmlStream& stream,
	std::map<uint64_t, std::ofstream>& outputs,
	const uint64_t groupEnd,
	const size_t fileSize,
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
				fileSize,
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
				readBlock(
					*block,
					stream,
					outputs,
					clusterTimecode,
					timecodeScale);
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

static void streamCluster(
	libebml::EbmlStream& stream,
	std::map<uint64_t, std::ofstream>& outputs,
	const size_t fileSize,
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
				fileSize,
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
					readBlock(
						*block,
						stream,
						outputs,
						clusterTimecode,
						timecodeScale);
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

			streamBlockGroup(
				stream,
				outputs,
				groupEnd,
				fileSize,
				clusterTimecode,
				timecodeScale);

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

static void streamSegment(
	libebml::EbmlStream& stream,
	const std::string& filename,
	std::map<uint64_t, std::ofstream>& pgsOutputs,
	const size_t fileSize)
{
	int upperLevel = 0;
	uint64_t timecodeScale = 1000000ULL;

	while (true)
	{
		libebml::EbmlElement* element =
			stream.FindNextElement(
				libmatroska::KaxSegment_Context,
				upperLevel,
				fileSize,
				false);

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
				readTracks(*tracks, stream, pgsOutputs, filename);
			}

			delete element;
			continue;
		}

		if (isElement(*element, EBML_ID(libmatroska::KaxCluster)))
		{
			const uint64_t dataStart = stream.I_O().getFilePointer();
			const uint64_t clusterEnd = getClusterEnd(*element, dataStart, fileSize);

			streamCluster(
				stream,
				pgsOutputs,
				fileSize,
				clusterEnd,
				timecodeScale);

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


MKVStreamer::MKVStreamer(const std::string& filename) :
    m_input_file_name(filename)
{

}

void MKVStreamer::run(void)
{
	std::map<uint64_t, std::ofstream> pgsOutputs;

	try
	{
		libebml::StdIOCallback file(m_input_file_name.c_str(), MODE_READ);
		libebml::EbmlStream stream(file);
		file.setFilePointer(0, libebml::seek_end);
		const size_t fileSize = file.getFilePointer();
		file.setFilePointer(0, libebml::seek_beginning);

		int upperLevel = 0;
		while (true)
		{
			libebml::EbmlElement* element =
				stream.FindNextElement(
					libmatroska::KaxMatroska_Context,
					upperLevel,
					fileSize,
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
				streamSegment(
					stream,
					m_input_file_name,
					pgsOutputs,
					fileSize);

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

	for (std::pair<const uint64_t, std::ofstream>& entry : pgsOutputs)
	{
		std::ofstream& output = entry.second;

		output.flush();
		output.close();
	}
}