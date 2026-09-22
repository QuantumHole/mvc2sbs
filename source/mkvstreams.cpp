#include <ebml/EbmlElement.h>
#include <ebml/EbmlStream.h>
#include <ebml/StdIOCallback.h>

#include <matroska/KaxContexts.h>
#include <matroska/KaxBlock.h>
#include <matroska/KaxTracks.h>

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <fstream>
#include <map>

// nutzt libEBML 1.4.7 und libMatroska 1.7.2

struct PgsTrack
{
	std::uint64_t number = 0;
	std::string codecId = "";
};

struct PgsOutput
{
	std::ofstream file = {};
	std::uint64_t frameCount = 0;
	std::uint64_t byteCount = 0;
};

static bool isElement(
	const libebml::EbmlElement& element,
	const libebml::EbmlId& id)
{
	return element.Generic().ClassId() == id;
}

static bool getTrackNumber(
	const libmatroska::KaxTrackEntry& trackEntry,
	std::uint64_t& number)
{
	const libebml::EbmlMaster* master =
		dynamic_cast<const libebml::EbmlMaster*>(&trackEntry);

	if (master == nullptr)
	{
		return false;
	}

	for (const libebml::EbmlElement* child : *master)
	{
		if (child == nullptr)
		{
			continue;
		}

		if (!isElement(*child, EBML_ID(libmatroska::KaxTrackNumber)))
		{
			continue;
		}

		const auto* trackNumber =
			dynamic_cast<const libmatroska::KaxTrackNumber*>(child);

		if (trackNumber == nullptr)
		{
			return false;
		}

		number = trackNumber->GetValue();
		return true;
	}

	return false;
}

static std::string getCodecId(
	const libmatroska::KaxTrackEntry& trackEntry)
{
	const libebml::EbmlMaster* master =
		dynamic_cast<const libebml::EbmlMaster*>(&trackEntry);

	if (master == nullptr)
	{
		return {};
	}

	for (const libebml::EbmlElement* child : *master)
	{
		if (child == nullptr)
		{
			continue;
		}

		if (!isElement(*child, EBML_ID(libmatroska::KaxCodecID)))
		{
			continue;
		}

		const auto* codecId =
			dynamic_cast<const libmatroska::KaxCodecID*>(child);

		if (codecId != nullptr)
		{
			return codecId->GetValue();
		}
	}

	return {};
}

static void findPgsTracks(
	const libebml::EbmlElement& element,
	std::map<std::uint64_t, PgsTrack>& tracks)
{
	if (isElement(element, EBML_ID(libmatroska::KaxTrackEntry)))
	{
		const auto* trackEntry =
			dynamic_cast<const libmatroska::KaxTrackEntry*>(&element);

		if (trackEntry != nullptr)
		{
			const std::string codecId = getCodecId(*trackEntry);

			if (codecId == "S_HDMV/PGS")
			{
				std::uint64_t number = 0;

				if (getTrackNumber(*trackEntry, number))
				{
					PgsTrack track;
					track.number = number;
					track.codecId = codecId;

					tracks[number] = std::move(track);
				}
			}
		}
	}

	const auto* master =
		dynamic_cast<const libebml::EbmlMaster*>(&element);

	if (master == nullptr)
	{
		return;
	}

	for (const libebml::EbmlElement* child : *master)
	{
		if (child != nullptr)
		{
			findPgsTracks(*child, tracks);
		}
	}
}

static std::string makeSupFilename(
	const std::string& inputFilename,
	std::uint64_t trackNumber)
{
	const std::size_t slash = inputFilename.find_last_of("/\\");

	const std::size_t filenameStart =
		(slash == std::string::npos) ? 0 : slash + 1;

	const std::size_t dot =
		inputFilename.find_last_of('.');

	const std::size_t extensionStart =
		(dot != std::string::npos && dot >= filenameStart)
			? dot
			: inputFilename.size();

	const std::string directory =
		(slash == std::string::npos)
			? std::string()
			: inputFilename.substr(0, slash + 1);

	const std::string baseName =
		inputFilename.substr(
			filenameStart,
			extensionStart - filenameStart);

	return directory
		+ baseName
		+ "_track"
		+ std::to_string(trackNumber)
		+ ".sup";
}

static void openPgsOutputs(
	const std::string& inputFilename,
	const std::map<std::uint64_t, PgsTrack>& tracks,
	std::map<std::uint64_t, PgsOutput>& outputs)
{
	for (const auto& entry : tracks)
	{
		const std::uint64_t trackNumber = entry.first;
		const std::string outputFilename =
			makeSupFilename(inputFilename, trackNumber);

		PgsOutput output;

		output.file.open(
			outputFilename,
			std::ios::binary |
			std::ios::out |
			std::ios::trunc);

		if (!output.file)
		{
			throw std::runtime_error(
				"Kann Datei nicht öffnen: " + outputFilename);
		}

		std::cout
			<< "PGS-Track "
			<< trackNumber
			<< " -> "
			<< outputFilename
			<< std::endl;

		outputs.emplace(trackNumber, std::move(output));
	}
}

static void extractPgsBlock(
	libmatroska::KaxInternalBlock& block,
	const std::map<std::uint64_t, PgsTrack>& tracks,
	std::map<std::uint64_t, PgsOutput>& outputs)
{
	const std::uint64_t trackNumber = block.TrackNum();

	if (tracks.find(trackNumber) == tracks.end())
	{
		return;
	}

	auto outputIt = outputs.find(trackNumber);

	if (outputIt == outputs.end())
	{
		return;
	}

	PgsOutput& output = outputIt->second;

	std::size_t totalFrameSize = 0;

	const std::size_t blockSize = block.GetSize();

	for (unsigned int frameNumber = 0;
	     frameNumber < block.NumberFrames();
	     ++frameNumber)
	{
		/*
		 * GetBuffer() liefert die vom Matroska-Blockheader
		 * getrennten Nutzdaten des jeweiligen Frames.
		 */
		libmatroska::DataBuffer& data =
			block.GetBuffer(frameNumber);

		const std::size_t frameSize = data.Size();

		totalFrameSize += frameSize;

		if (data.Buffer() == nullptr ||
		    totalFrameSize > blockSize)
		{
			std::cerr
				<< "Ungültiger Frame in Track "
				<< trackNumber
				<< std::endl;

			break;
		}

		output.file.write(
			reinterpret_cast<const char*>(data.Buffer()),
			static_cast<std::streamsize>(frameSize));

		if (!output.file)
		{
			throw std::runtime_error(
				"Fehler beim Schreiben von Track " +
				std::to_string(trackNumber));
		}

		++output.frameCount;
		output.byteCount += frameSize;
	}
}

static void extractPgsFromElement(
	libebml::EbmlElement& element,
	const std::map<std::uint64_t, PgsTrack>& tracks,
	std::map<std::uint64_t, PgsOutput>& outputs)
{
	/*
	 * KaxBlock und KaxSimpleBlock sind von KaxInternalBlock
	 * abgeleitet. Damit werden sowohl BlockGroup/KaxBlock als
	 * auch SimpleBlock unterstützt.
	 */
	auto* block =
		dynamic_cast<libmatroska::KaxInternalBlock*>(&element);

	if (block != nullptr)
	{
		extractPgsBlock(*block, tracks, outputs);
		return;
	}

	auto* master =
		dynamic_cast<libebml::EbmlMaster*>(&element);

	if (master == nullptr)
	{
		return;
	}

	for (libebml::EbmlElement* child : *master)
	{
		if (child != nullptr)
		{
			extractPgsFromElement(*child, tracks, outputs);
		}
	}
}

static std::string formatId(const libebml::EbmlId& id)
{
	std::ostringstream out;

	// GetLength() liefert size_t, std::setw() erwartet int.
	const int width = static_cast<int>(id.GetLength() * 2);

	out << "0x" << std::uppercase << std::hex << std::setfill('0') << std::setw(width)
	    << id.GetValue();

	return out.str();
}

static void printValue(const libebml::EbmlElement& element)
{
	if (const libebml::EbmlUInteger* value = dynamic_cast<const libebml::EbmlUInteger*>(&element))
	{
		std::cout << " = " << value->GetValue();
		return;
	}

	if (const libebml::EbmlSInteger* value = dynamic_cast<const libebml::EbmlSInteger*>(&element))
	{
		std::cout << " = " << value->GetValue();
		return;
	}

	if (const libebml::EbmlFloat* value = dynamic_cast<const libebml::EbmlFloat*>(&element))
	{
		std::cout << " = " << value->GetValue();
		return;
	}

	if (const libebml::EbmlDate* value = dynamic_cast<const libebml::EbmlDate*>(&element))
	{
		std::cout << " = " << value->GetEpochDate();
		return;
	}

	if (const libebml::EbmlString* value = dynamic_cast<const libebml::EbmlString*>(&element))
	{
		std::cout << " = \"" << value->GetValue() << "\"";
		return;
	}

	if (const libebml::EbmlUnicodeString* value = dynamic_cast<const libebml::EbmlUnicodeString*>(&element))
	{
		std::cout << " = \"" << value->GetValueUTF8() << "\"";
		return;
	}
}

static void dumpElement(
	const libebml::EbmlElement& element,
	const unsigned int depth)
{
	const libebml::EbmlCallbacks& callbacks = element.Generic();

    // const std::string name = callbacks.GetName();
    // if (
    //     (name == "Cluster") ||
    //     (name == "Chapters") ||
    //     (name == "Tags") ||
    //     (name == "Cues")
    // )
    // {
    //     return;
    // }

    std::cout << std::string(2 * depth, ' ');
  
	std::cout
	    << callbacks.GetName()
	    << " [" << formatId(callbacks.ClassId()) << "]"
	    << " @ " << std::dec << element.GetElementPosition();

	if (element.IsFiniteSize())
	{
		std::cout << ", size=" << element.GetSize();
	}
	else
	{
		std::cout << ", size=unknown";
	}

	if (!element.IsMaster())
	{
		printValue(element);
	}

	std::cout << std::endl;

	const libebml::EbmlMaster* master = dynamic_cast<const libebml::EbmlMaster*>(&element);
	if (master == nullptr)
	{
		return;
	}

	for (const libebml::EbmlElement* child : *master)
	{
		if (child != nullptr)
		{
			dumpElement(*child, depth + 1);
		}
	}
}

static libebml::EbmlElement* readNextRootElement(
	libebml::EbmlStream& stream,
	const size_t maxDataSize,
	int& upperLevel)
{
	/*
	 * KaxMatroska_Context beschreibt die oberste Matroska-Ebene:
	 */
	return stream.FindNextElement(
		libmatroska::KaxMatroska_Context,
		upperLevel,
		maxDataSize,
		false);
}

int main(const int argc, const char** argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: " << argv[0] << " <file.mkv>" << std::endl;
		return 1;
	}

    const std::string filename = argv[1];

    std::map<std::uint64_t, PgsTrack> pgsTracks;
    std::map<std::uint64_t, PgsOutput> pgsOutputs;

	try
	{
		libebml::StdIOCallback file(filename.c_str(), MODE_READ);
		libebml::EbmlStream stream(file);

		/*
		 * Dateigröße bestimmen.
		 *
		 * IOCallback besitzt keinen getFileSize(), aber
		 * setFilePointer()/getFilePointer() sind Teil der API.
		 */
		file.setFilePointer(0, libebml::seek_end);
		const size_t fileSize = file.getFilePointer();
		file.setFilePointer(0, libebml::seek_beginning);

		/*
		 * Die oberste Matroska-Kontextdefinition kennt EBML Header
		 * und Segment.
		 */
		int upperLevel = 0;

while (true)
{
	libebml::EbmlElement* element =
		readNextRootElement(stream, fileSize, upperLevel);

	if (element == nullptr)
	{
		break;
	}

	int childUpperLevel = 0;
	libebml::EbmlElement* foundElement = nullptr;

	element->Read(
		stream,
		element->Generic().GetContext(),
		childUpperLevel,
		foundElement,
		false,
		libebml::SCOPE_ALL_DATA);

	dumpElement(*element, 0);

	/*
	 * TrackEntry-Elemente suchen.
	 */
	findPgsTracks(*element, pgsTracks);

	/*
	 * Da das Segment mit SCOPE_ALL_DATA gelesen wird, sind die
	 * TrackEntry- und Cluster-Elemente normalerweise im selben
	 * Baum vorhanden.
	 */
	if (!pgsTracks.empty())
	{
		if (pgsOutputs.empty())
		{
			openPgsOutputs(filename, pgsTracks, pgsOutputs);
		}

		/*
		 * Muss vor delete element ausgeführt werden.
		 */
		extractPgsFromElement(
			*element,
			pgsTracks,
			pgsOutputs);
	}

	delete element;

	if (foundElement != nullptr)
	{
		delete foundElement;
		foundElement = nullptr;
	}

	if (file.getFilePointer() >= fileSize)
	{
		break;
	}

	upperLevel = 0;
}
    }
	catch (const std::exception& e)
	{
		std::cerr << "Error while reading '" << filename << "': " << e.what() << std::endl;

		return 1;
	}

for (const auto& entry : pgsOutputs)
{
	const std::uint64_t trackNumber = entry.first;
	const PgsOutput& output = entry.second;

	std::cout
		<< "Track "
		<< trackNumber
		<< ": "
		<< output.frameCount
		<< " Frames, "
		<< output.byteCount
		<< " Bytes"
		<< std::endl;
}


	return 0;
}
