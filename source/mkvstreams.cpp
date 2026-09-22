#include <ebml/EbmlElement.h>
#include <ebml/EbmlStream.h>
#include <ebml/EbmlHead.h>
#include <ebml/StdIOCallback.h>

#include <matroska/KaxContexts.h>
#include <matroska/KaxBlock.h>
#include <matroska/KaxTracks.h>
#include <matroska/KaxCluster.h>
#include <matroska/KaxSegment.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>


// ============================================================================
// libEBML 1.4.7
// libMatroska 1.7.2
// ============================================================================


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


// ============================================================================
// Hilfsfunktionen
// ============================================================================

static bool isElement(
	const libebml::EbmlElement& element,
	const libebml::EbmlId& id)
{
	return element.Generic().ClassId() == id;
}


// ============================================================================
// Tracknummer aus TrackEntry
// ============================================================================

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

		if (!isElement(
				*child,
				EBML_ID(libmatroska::KaxTrackNumber)))
		{
			continue;
		}

		const libmatroska::KaxTrackNumber* trackNumber =
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


// ============================================================================
// Codec-ID aus TrackEntry
// ============================================================================

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

		if (!isElement(
				*child,
				EBML_ID(libmatroska::KaxCodecID)))
		{
			continue;
		}

		const libmatroska::KaxCodecID* codecId =
			dynamic_cast<const libmatroska::KaxCodecID*>(child);

		if (codecId != nullptr)
		{
			return codecId->GetValue();
		}
	}

	return {};
}


// ============================================================================
// Ausgabedateiname
// ============================================================================

static std::string makeSupFilename(
	const std::string& inputFilename,
	std::uint64_t trackNumber)
{
	const std::size_t slash =
		inputFilename.find_last_of("/\\");

	const std::size_t filenameStart =
		(slash == std::string::npos)
			? 0
			: slash + 1;

	const std::size_t dot =
		inputFilename.find_last_of('.');

	const std::size_t extensionStart =
		(dot != std::string::npos &&
		 dot >= filenameStart)
			? dot
			: inputFilename.size();

	const std::string directory =
		(slash == std::string::npos)
			? std::string()
			: inputFilename.substr(
				0,
				slash + 1);

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


// ============================================================================
// Ausgabedateien öffnen
// ============================================================================

static void openPgsOutputs(
	const std::string& inputFilename,
	const std::map<std::uint64_t, PgsTrack>& tracks,
	std::map<std::uint64_t, PgsOutput>& outputs)
{
	for (const auto& entry : tracks)
	{
		const std::uint64_t trackNumber =
			entry.first;

		const std::string outputFilename =
			makeSupFilename(
				inputFilename,
				trackNumber);

		PgsOutput output;

		output.file.open(
			outputFilename,
			std::ios::binary |
			std::ios::out |
			std::ios::trunc);

		if (!output.file)
		{
			throw std::runtime_error(
				"Kann Datei nicht öffnen: " +
				outputFilename);
		}

		std::cout
			<< "PGS-Track "
			<< trackNumber
			<< " -> "
			<< outputFilename
			<< std::endl;

		outputs.emplace(
			trackNumber,
			std::move(output));
	}
}


// ============================================================================
// PGS-Block auswerten
//
// Wird erst NACH ReadData() aufgerufen.
// ============================================================================

static void extractPgsBlock(
	libmatroska::KaxInternalBlock& block,
	const std::map<std::uint64_t, PgsTrack>& tracks,
	std::map<std::uint64_t, PgsOutput>& outputs)
{
	const std::uint64_t trackNumber =
		block.TrackNum();

	const auto trackIt =
		tracks.find(trackNumber);

	if (trackIt == tracks.end())
	{
		return;
	}

	const auto outputIt =
		outputs.find(trackNumber);

	if (outputIt == outputs.end())
	{
		return;
	}

	PgsOutput& output =
		outputIt->second;

	const std::size_t blockSize =
		block.GetSize();

	std::cout
		<< "    PGS block: track="
		<< trackNumber
		<< ", frames="
		<< block.NumberFrames()
		<< ", size="
		<< blockSize
		<< std::endl;

	std::size_t totalFrameSize = 0;

	for (unsigned int frameNumber = 0;
	     frameNumber < block.NumberFrames();
	     ++frameNumber)
	{
		libmatroska::DataBuffer& data =
			block.GetBuffer(frameNumber);

		const std::size_t frameSize =
			data.Size();

		totalFrameSize += frameSize;

		/*
		 * Die Frame-Nutzdaten dürfen nicht größer sein
		 * als der Block.
		 */
		if (data.Buffer() == nullptr ||
		    totalFrameSize > blockSize)
		{
			std::cerr
				<< "    Ungültiger Frame in Track "
				<< trackNumber
				<< std::endl;

			return;
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


// ============================================================================
// Block lesen
//
// WICHTIG:
//
// FindNextElement() liest zunächst nur den EBML-Header.
//
// ReadData() liest anschließend den eigentlichen Blockinhalt.
//
// Erst DANACH darf TrackNum(), NumberFrames() und GetBuffer()
// verwendet werden.
// ============================================================================

static bool readBlock(
	libmatroska::KaxInternalBlock& block,
	libebml::EbmlStream& stream,
	const std::map<std::uint64_t, PgsTrack>& tracks,
	std::map<std::uint64_t, PgsOutput>& outputs)
{
	/*
	 * Der Element-Header wurde bereits gelesen.
	 *
	 * Jetzt den tatsächlichen Blockinhalt lesen.
	 */
	block.ReadData(
		stream.I_O(),
		libebml::SCOPE_ALL_DATA);

	const std::uint64_t trackNumber =
		block.TrackNum();

	std::cout
		<< "  Block: track="
		<< trackNumber
		<< " @ "
		<< block.GetElementPosition()
		<< ", size="
		<< block.GetSize()
		<< std::endl;

	if (tracks.find(trackNumber) == tracks.end())
	{
		/*
		 * Kein PGS-Track.
		 */
		return true;
	}

	extractPgsBlock(
		block,
		tracks,
		outputs);

	return true;
}


// ============================================================================
// Tracks lesen
// ============================================================================

static void readTracks(
	libmatroska::KaxTracks& tracksElement,
	libebml::EbmlStream& stream,
	std::map<std::uint64_t, PgsTrack>& pgsTracks)
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

	if (foundElement != nullptr)
	{
		delete foundElement;
		foundElement = nullptr;
	}

	const libebml::EbmlMaster* master =
		dynamic_cast<const libebml::EbmlMaster*>(
			&tracksElement);

	if (master == nullptr)
	{
		return;
	}

	for (const libebml::EbmlElement* child : *master)
	{
		if (child == nullptr)
		{
			continue;
		}

		if (!isElement(
				*child,
				EBML_ID(libmatroska::KaxTrackEntry)))
		{
			continue;
		}

		const libmatroska::KaxTrackEntry* trackEntry =
			dynamic_cast<const libmatroska::KaxTrackEntry*>(
				child);

		if (trackEntry == nullptr)
		{
			continue;
		}

		const std::string codecId =
			getCodecId(*trackEntry);

		if (codecId != "S_HDMV/PGS")
		{
			continue;
		}

		std::uint64_t trackNumber = 0;

		if (!getTrackNumber(
				*trackEntry,
				trackNumber))
		{
			continue;
		}

		PgsTrack track;
		track.number = trackNumber;
		track.codecId = codecId;

		pgsTracks[trackNumber] =
			std::move(track);

		std::cout
			<< "PGS-Track gefunden: "
			<< trackNumber
			<< std::endl;
	}
}


// ============================================================================
// Cluster-Ende bestimmen
//
// clusterDataStart ist die Position direkt hinter dem Cluster-Header.
// ============================================================================

static std::uint64_t getClusterEnd(
	const libebml::EbmlElement& cluster,
	std::uint64_t clusterDataStart,
	std::size_t fileSize)
{
	if (!cluster.IsFiniteSize())
	{
		return static_cast<std::uint64_t>(fileSize);
	}

	const std::uint64_t size =
		cluster.GetSize();

	const std::uint64_t end =
		clusterDataStart + size;

	if (end > static_cast<std::uint64_t>(fileSize))
	{
		return static_cast<std::uint64_t>(fileSize);
	}

	return end;
}


// ============================================================================
// BlockGroup streamen
// ============================================================================

static void streamBlockGroup(
	libebml::EbmlStream& stream,
	const std::map<std::uint64_t, PgsTrack>& tracks,
	std::map<std::uint64_t, PgsOutput>& outputs,
	std::uint64_t groupEnd,
	std::size_t fileSize)
{
	int upperLevel = 0;

	while (true)
	{
		const std::uint64_t currentPosition =
			fileSize == 0
				? 0
				: stream.I_O().getFilePointer();

		if (currentPosition >= groupEnd)
		{
			return;
		}

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

		const std::uint64_t elementPosition =
			element->GetElementPosition();

		std::cout
			<< "    BlockGroup child: "
			<< element->Generic().GetName()
			<< " @ "
			<< elementPosition;

		if (element->IsFiniteSize())
		{
			std::cout
				<< ", size="
				<< element->GetSize();
		}

		std::cout
			<< std::endl;


		/*
		 * Ist das Element außerhalb der BlockGroup,
		 * sind wir fertig.
		 */
		if (elementPosition >= groupEnd)
		{
			delete element;
			return;
		}


		/*
		 * Bei einem Block muss zunächst ReadData()
		 * ausgeführt werden.
		 */
		libmatroska::KaxInternalBlock* block =
			dynamic_cast<libmatroska::KaxInternalBlock*>(
				element);

		if (block != nullptr)
		{
			const std::uint64_t dataStart =
				stream.I_O().getFilePointer();

			const std::uint64_t blockSize =
				block->GetSize();

			if (block->IsFiniteSize() &&
			    (dataStart > groupEnd ||
			     blockSize > groupEnd - dataStart))
			{
				std::cerr
					<< "    Beschädigter Block in BlockGroup: "
					<< "size="
					<< blockSize
					<< ", verbleibend="
					<< (dataStart < groupEnd
							? groupEnd - dataStart
							: 0)
					<< ". BlockGroup wird übersprungen."
					<< std::endl;

				delete element;

				stream.I_O().setFilePointer(
					static_cast<std::int64_t>(groupEnd),
					libebml::seek_beginning);

				return;
			}

			try
			{
				readBlock(
					*block,
					stream,
					tracks,
					outputs);
			}
			catch (const std::exception& e)
			{
				std::cerr
					<< "    Fehler beim Lesen des Blocks: "
					<< e.what()
					<< std::endl;

				delete element;

				stream.I_O().setFilePointer(
					static_cast<std::int64_t>(groupEnd),
					libebml::seek_beginning);

				return;
			}

			delete element;
			continue;
		}


		/*
		 * Alle anderen BlockGroup-Elemente überspringen.
		 */
		try
		{
			element->SkipData(
				stream,
				element->Generic().Context);
		}
		catch (const std::exception& e)
		{
			std::cerr
				<< "    Fehler beim Überspringen von "
				<< element->Generic().GetName()
				<< ": "
				<< e.what()
				<< std::endl;

			delete element;

			stream.I_O().setFilePointer(
				static_cast<std::int64_t>(groupEnd),
				libebml::seek_beginning);

			return;
		}

		delete element;
	}
}


// ============================================================================
// Cluster streamen
// ============================================================================

static void streamCluster(
	libebml::EbmlStream& stream,
	const std::map<std::uint64_t, PgsTrack>& tracks,
	std::map<std::uint64_t, PgsOutput>& outputs,
	std::size_t fileSize,
	std::uint64_t clusterEnd)
{
	int upperLevel = 0;

	while (true)
	{
		const std::uint64_t currentPosition =
			stream.I_O().getFilePointer();

		if (currentPosition >= clusterEnd)
		{
			std::cout
				<< "Cluster: Ende"
				<< std::endl;

			return;
		}


		libebml::EbmlElement* element =
			stream.FindNextElement(
				libmatroska::KaxCluster_Context,
				upperLevel,
				fileSize,
				false);

		if (element == nullptr)
		{
			std::cout
				<< "Cluster: Ende"
				<< std::endl;

			/*
			 * Bei einem beschädigten Cluster sicher zum
			 * Clusterende springen.
			 */
			stream.I_O().setFilePointer(
				static_cast<std::int64_t>(clusterEnd),
				libebml::seek_beginning);

			return;
		}


		const std::uint64_t elementPosition =
			element->GetElementPosition();

		std::cout
			<< "Cluster child: "
			<< element->Generic().GetName()
			<< " @ "
			<< elementPosition;

		if (element->IsFiniteSize())
		{
			std::cout
				<< ", size="
				<< element->GetSize();
		}
		else
		{
			std::cout
				<< ", size=unknown";
		}

		std::cout
			<< ", upperLevel="
			<< upperLevel
			<< std::endl;


		/*
		 * Element gehört nicht mehr zum Cluster.
		 */
		if (elementPosition >= clusterEnd)
		{
			delete element;

			stream.I_O().setFilePointer(
				static_cast<std::int64_t>(clusterEnd),
				libebml::seek_beginning);

			return;
		}


		/*
		 * ---------------------------------------------------------------
		 * SimpleBlock
		 * ---------------------------------------------------------------
		 */

		libmatroska::KaxInternalBlock* block =
			dynamic_cast<libmatroska::KaxInternalBlock*>(
				element);

		if (block != nullptr)
		{
			const std::uint64_t dataStart =
				stream.I_O().getFilePointer();

			const std::uint64_t blockSize =
				block->GetSize();


			/*
			 * WICHTIGER KORRUPTIONSCHECK
			 *
			 * Ein Block darf nicht über das Ende des Clusters
			 * hinausragen.
			 *
			 * Das fängt insbesondere den beobachteten
			 * 48-MB-Block ab.
			 */
			if (block->IsFiniteSize() &&
			    (dataStart > clusterEnd ||
			     blockSize > clusterEnd - dataStart))
			{
				std::cerr
					<< "  Beschädigter Block erkannt: "
					<< "Position="
					<< elementPosition
					<< ", Größe="
					<< blockSize
					<< ", verbleibend im Cluster="
					<< (dataStart < clusterEnd
							? clusterEnd - dataStart
							: 0)
					<< std::endl;

				std::cerr
					<< "  Rest des Clusters wird übersprungen."
					<< std::endl;

				delete element;

				stream.I_O().setFilePointer(
					static_cast<std::int64_t>(clusterEnd),
					libebml::seek_beginning);

				return;
			}


			/*
			 * Der Block ist plausibel.
			 *
			 * Jetzt erst den tatsächlichen Blockinhalt lesen.
			 */
			try
			{
				readBlock(
					*block,
					stream,
					tracks,
					outputs);
			}
			catch (const std::exception& e)
			{
				std::cerr
					<< "  Fehler beim Lesen des Blocks: "
					<< e.what()
					<< std::endl;

				/*
				 * Bei beschädigten Daten nicht versuchen,
				 * mitten im Block weiter zu parsen.
				 *
				 * Stattdessen zum Clusterende springen.
				 */
				delete element;

				stream.I_O().setFilePointer(
					static_cast<std::int64_t>(clusterEnd),
					libebml::seek_beginning);

				return;
			}

			delete element;
			continue;
		}


		/*
		 * ---------------------------------------------------------------
		 * BlockGroup
		 * ---------------------------------------------------------------
		 */

		if (isElement(
				*element,
				EBML_ID(libmatroska::KaxBlockGroup)))
		{
			const std::uint64_t groupDataStart =
				stream.I_O().getFilePointer();

			const std::uint64_t groupSize =
				element->GetSize();

			std::uint64_t groupEnd =
				clusterEnd;

			if (element->IsFiniteSize())
			{
				if (groupDataStart > clusterEnd ||
				    groupSize > clusterEnd - groupDataStart)
				{
					std::cerr
						<< "  Beschädigte BlockGroup erkannt."
						<< " Rest des Clusters wird übersprungen."
						<< std::endl;

					delete element;

					stream.I_O().setFilePointer(
						static_cast<std::int64_t>(clusterEnd),
						libebml::seek_beginning);

					return;
				}

				groupEnd =
					groupDataStart + groupSize;
			}

			std::cout
				<< "  Entering BlockGroup"
				<< std::endl;

			streamBlockGroup(
				stream,
				tracks,
				outputs,
				groupEnd,
				fileSize);

			std::cout
				<< "  Leaving BlockGroup"
				<< std::endl;

			delete element;
			continue;
		}


		/*
		 * ---------------------------------------------------------------
		 * Sonstige Cluster-Elemente
		 * ---------------------------------------------------------------
		 *
		 * Timecode
		 * Position
		 * PrevSize
		 * EBMLVoid
		 * EncryptedBlock
		 * usw.
		 */

		const std::uint64_t dataStart =
			stream.I_O().getFilePointer();

		if (element->IsFiniteSize())
		{
			const std::uint64_t size =
				element->GetSize();

			if (dataStart > clusterEnd ||
			    size > clusterEnd - dataStart)
			{
				std::cerr
					<< "  Beschädigtes Cluster-Element "
					<< element->Generic().GetName()
					<< " erkannt."
					<< std::endl;

				delete element;

				stream.I_O().setFilePointer(
					static_cast<std::int64_t>(clusterEnd),
					libebml::seek_beginning);

				return;
			}
		}

		try
		{
			element->SkipData(
				stream,
				element->Generic().Context);
		}
		catch (const std::exception& e)
		{
			std::cerr
				<< "  Fehler beim Überspringen von "
				<< element->Generic().GetName()
				<< ": "
				<< e.what()
				<< std::endl;

			delete element;

			stream.I_O().setFilePointer(
				static_cast<std::int64_t>(clusterEnd),
				libebml::seek_beginning);

			return;
		}

		delete element;
	}
}


// ============================================================================
// Segment streamen
// ============================================================================

static void streamSegment(
	libebml::EbmlStream& stream,
	const std::string& filename,
	std::map<std::uint64_t, PgsTrack>& pgsTracks,
	std::map<std::uint64_t, PgsOutput>& pgsOutputs,
	std::size_t fileSize)
{
	int upperLevel = 0;

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
			std::cout
				<< "Segment: Ende"
				<< std::endl;

			return;
		}


		std::cout
			<< "Segment child: "
			<< element->Generic().GetName()
			<< " @ "
			<< element->GetElementPosition();

		if (element->IsFiniteSize())
		{
			std::cout
				<< ", size="
				<< element->GetSize();
		}
		else
		{
			std::cout
				<< ", size=unknown";
		}

		std::cout
			<< ", upperLevel="
			<< upperLevel
			<< std::endl;


		/*
		 * ---------------------------------------------------------------
		 * Tracks
		 * ---------------------------------------------------------------
		 */

		if (isElement(
				*element,
				EBML_ID(libmatroska::KaxTracks)))
		{
			libmatroska::KaxTracks* tracks =
				dynamic_cast<libmatroska::KaxTracks*>(
					element);

			if (tracks != nullptr)
			{
				std::cout
					<< "Reading Tracks..."
					<< std::endl;

				readTracks(
					*tracks,
					stream,
					pgsTracks);

				if (!pgsTracks.empty() &&
				    pgsOutputs.empty())
				{
					openPgsOutputs(
						filename,
						pgsTracks,
						pgsOutputs);
				}
			}

			delete element;
			continue;
		}


		/*
		 * ---------------------------------------------------------------
		 * Cluster
		 * ---------------------------------------------------------------
		 */

		if (isElement(
				*element,
				EBML_ID(libmatroska::KaxCluster)))
		{
			const std::uint64_t clusterPosition =
				element->GetElementPosition();

			/*
			 * FindNextElement() hat den Cluster-Header bereits
			 * gelesen. Der Stream steht deshalb direkt am Anfang
			 * der Cluster-Daten.
			 */
			const std::uint64_t clusterDataStart =
				stream.I_O().getFilePointer();

			const std::uint64_t clusterEnd =
				getClusterEnd(
					*element,
					clusterDataStart,
					fileSize);

			std::cout
				<< "Streaming Cluster @ "
				<< clusterPosition
				<< ", dataStart="
				<< clusterDataStart
				<< ", end="
				<< clusterEnd
				<< std::endl;


			streamCluster(
				stream,
				pgsTracks,
				pgsOutputs,
				fileSize,
				clusterEnd);

			delete element;

			continue;
		}


		/*
		 * ---------------------------------------------------------------
		 * Sonstige Segment-Elemente
		 * ---------------------------------------------------------------
		 */

		try
		{
			element->SkipData(
				stream,
				element->Generic().Context);
		}
		catch (const std::exception& e)
		{
			std::cerr
				<< "Fehler beim Überspringen von "
				<< element->Generic().GetName()
				<< ": "
				<< e.what()
				<< std::endl;

			delete element;
			return;
		}

		delete element;
	}
}


// ============================================================================
// main
// ============================================================================

int main(
	const int argc,
	const char** argv)
{
	if (argc != 2)
	{
		std::cerr
			<< "Usage: "
			<< argv[0]
			<< " <file.mkv>"
			<< std::endl;

		return 1;
	}


	const std::string filename =
		argv[1];

	std::map<std::uint64_t, PgsTrack> pgsTracks;
	std::map<std::uint64_t, PgsOutput> pgsOutputs;


	try
	{
		libebml::StdIOCallback file(
			filename.c_str(),
			MODE_READ);

		libebml::EbmlStream stream(file);


		/*
		 * Dateigröße bestimmen.
		 */
		file.setFilePointer(
			0,
			libebml::seek_end);

		const std::size_t fileSize =
			file.getFilePointer();

		file.setFilePointer(
			0,
			libebml::seek_beginning);


		std::cout
			<< "file size: "
			<< fileSize
			<< " bytes"
			<< std::endl;


		/*
		 * ================================================================
		 * Top-Level-Elemente
		 * ================================================================
		 */

		int upperLevel = 0;

		while (true)
		{
			std::cout
				<< "Searching next top-level element..."
				<< std::endl;


			libebml::EbmlElement* element =
				stream.FindNextElement(
					libmatroska::KaxMatroska_Context,
					upperLevel,
					fileSize,
					false);

			if (element == nullptr)
			{
				std::cout
					<< "End of file."
					<< std::endl;

				break;
			}


			std::cout
				<< "Found: "
				<< element->Generic().GetName()
				<< " @ "
				<< element->GetElementPosition()
				<< std::endl;


			/*
			 * ============================================================
			 * EBML Header
			 * ============================================================
			 */

			if (isElement(
					*element,
					EBML_ID(libebml::EbmlHead)))
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


			/*
			 * ============================================================
			 * Segment
			 * ============================================================
			 */

			if (isElement(
					*element,
					EBML_ID(libmatroska::KaxSegment)))
			{
				std::cout
					<< "Streaming Segment..."
					<< std::endl;


				streamSegment(
					stream,
					filename,
					pgsTracks,
					pgsOutputs,
					fileSize);


				delete element;

				break;
			}


			/*
			 * Unbekanntes Top-Level-Element.
			 */
			try
			{
				element->SkipData(
					stream,
					element->Generic().Context);
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
		std::cerr
			<< "Error while reading '"
			<< filename
			<< "': "
			<< e.what()
			<< std::endl;

		return 1;
	}


	/*
	 * ================================================================
	 * Statistik
	 * ================================================================
	 */

	for (const auto& entry : pgsOutputs)
	{
		const std::uint64_t trackNumber =
			entry.first;

		const PgsOutput& output =
			entry.second;

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
