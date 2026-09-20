#include <cstdlib>
#include <exception>
#include <iostream>

#include <ebml/EbmlHead.h>
#include <ebml/EbmlStream.h>
#include <ebml/StdIOCallback.h>

#include <matroska/KaxSegment.h>
#include <matroska/KaxTracks.h>
#include <matroska/KaxTrackEntryData.h>
#include <matroska/KaxTrackAudio.h>
#include <matroska/KaxTrackVideo.h>

using namespace libebml;
using namespace libmatroska;

template <typename Type>
Type *findChild(const EbmlMaster &master)
{
    EbmlElement *element =
        master.FindFirstElt(EBML_INFO(Type));

    return static_cast<Type *>(element);
}


int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "Verwendung: " << argv[0] << " datei.mkv\n";
        return EXIT_FAILURE;
    }

    try
    {
        StdIOCallback input(argv[1], MODE_READ);
        EbmlStream stream(input);

		std::cout << "Datei geöffnet: " << argv[1] << std::endl;

        int upperLevel = 0;

        /*
         * 1. EBML-Header suchen.
         *
         * Bei FindNextElement wird nicht nach einer Element-ID gesucht,
         * sondern ein semantischer Kontext übergeben.
         */
        EbmlElement *element = stream.FindNextElement(
            EbmlHead::ClassInfos.Context,
            upperLevel,
            0xFFFFFFFFFFFFFFFFULL,
            false,
            1
        );

        if (element == nullptr)
        {
            std::cerr << "Kein EBML-Header gefunden.\n";
            return EXIT_FAILURE;
        }

        auto *ebmlHeader = dynamic_cast<EbmlHead *>(element);
        if (ebmlHeader == nullptr)
        {
            std::cerr << "Das erste Element ist kein EBML-Header.\n";
            delete element;
            return EXIT_FAILURE;
        }

        std::cout << "EBML Header gefunden." << std::endl;

        ebmlHeader->ReadData(input, SCOPE_ALL_DATA);
        delete ebmlHeader;

        /*
         * 2. Segment suchen.
         *
         * Nach dem Header wird der Suchkontext auf den Matroska-Kontext
         * umgestellt.
         */
        element = stream.FindNextElement(
            KaxSegment::ClassInfos.Context,
            upperLevel,
            0xFFFFFFFFFFFFFFFFULL,
            false,
            1
        );

        if (element == nullptr)
        {
            std::cerr << "Kein Matroska-Segment gefunden.\n";
            return EXIT_FAILURE;
        }

        auto *segment = dynamic_cast<KaxSegment *>(element);
        if (segment == nullptr)
        {
            std::cerr << "Das gefundene Element ist kein KaxSegment.\n";
            delete element;
            return EXIT_FAILURE;
        }

        /*
         * 3. Den Inhalt des Segments einlesen.
         */
        segment->ReadData(input, SCOPE_ALL_DATA);


		std::cerr << "Segment gefunden\n";
		size_t size = segment->GetSize();
		if (size == 0xFFFFFFFFFFFFFFFFULL)
		{
			std::cout << "Segmentgröße: unbekannt\n";
		}
		else
		{
			std::cout << "Segmentgröße: " << size << " Bytes\n";
		}

        KaxTracks *tracks = nullptr;

        /*
         * 4. KaxTracks innerhalb des Segments suchen.
         */
		for (EbmlElement *child : segment->GetElementList())
        {
			// std::cerr << "Element-ID: 0x"
			// 		<< std::hex
			// 		<< child->GetElementId().GetValue()
			// 		<< std::dec
			// 		<< "\n";

			if (auto *candidate = dynamic_cast<KaxTracks *>(child))
            {
                tracks = candidate;
                break;
            }
        }

        if (tracks == nullptr)
        {
            std::cout << "Keine Tracks gefunden.\n";
            delete segment;
            return EXIT_SUCCESS;
        }

        std::cout << "Datei: " << argv[1] << "\n";
        std::cout << "Streams:\n\n";

        int streamIndex = 0;

        /*
         * 5. Alle TrackEntry-Elemente ausgeben.
         */
		for (EbmlElement *child : tracks->GetElementList())
        {
            auto *track = dynamic_cast<KaxTrackEntry *>(child);

            if (track == nullptr)
                continue;

            ++streamIndex;

            auto *trackNumber = FindChild<KaxTrackNumber>(*track);

            auto *trackType = FindChild<KaxTrackType>(*track);

            auto *codecId = FindChild<KaxCodecID>(*track);

            auto *codecName = FindChild<KaxCodecName>(*track);

            auto *language = FindChild<KaxTrackLanguage>(*track);

            auto *trackName = FindChild<KaxTrackName>(*track);

            std::cout << "Stream #" << streamIndex << "\n";

            if (trackNumber != nullptr)
            {
                std::cout << "  Tracknummer: "
                          << trackNumber->GetValue() << "\n";
            }

            uint64_t type = 0;

            if (trackType != nullptr)
            {
                type = trackType->GetValue();

                std::cout << "  Typ: ";

                switch (type)
                {
                    case 1:
                        std::cout << "Video";
                        break;

                    case 2:
                        std::cout << "Audio";
                        break;

                    case 3:
                        std::cout << "Untertitel";
                        break;

                    default:
                        std::cout << "Unbekannt";
                        break;
                }

                std::cout << " (" << type << ")\n";
            }

            if (codecId != nullptr)
            {
                std::cout << "  Codec-ID: "
                          << codecId->GetValue() << "\n";
            }

            if (codecName != nullptr)
            {
                std::cout << "  Codecname: "
                          << codecName->GetValue() << "\n";
            }

            if (language != nullptr)
            {
                std::cout << "  Sprache: "
                          << language->GetValue() << "\n";
            }

            if (trackName != nullptr)
            {
                std::cout << "  Name: "
                          << trackName->GetValue() << "\n";
            }

            /*
             * Zusätzliche Video-Informationen.
             */
            if (type == 1)
            {
                auto *video = FindChild<KaxTrackVideo>(*track);

                if (video != nullptr)
                {
                    auto *width = FindChild<KaxVideoPixelWidth>(*track);

                    auto *height = FindChild<KaxVideoPixelHeight>(*track);

                    if (width != nullptr && height != nullptr)
                    {
                        std::cout << "  Auflösung: "
                                  << width->GetValue()
                                  << "x"
                                  << height->GetValue()
                                  << "\n";
                    }
                }
            }

            /*
             * Zusätzliche Audio-Informationen.
             */
            if (type == 2)
            {
                auto *audio = FindChild<KaxTrackAudio>(*track);

                if (audio != nullptr)
                {
                    auto *sampleRate = FindChild<KaxAudioSamplingFreq>(*track);

                    auto *channels = FindChild<KaxAudioChannels>(*track);

                    if (sampleRate != nullptr)
                    {
                        std::cout << "  Abtastrate: "
                                  << sampleRate->GetValue()
                                  << " Hz\n";
                    }

                    if (channels != nullptr)
                    {
                        std::cout << "  Kanäle: "
                                  << channels->GetValue()
                                  << "\n";
                    }
                }
            }

            std::cout << "\n";
        }

        delete segment;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fehler: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
