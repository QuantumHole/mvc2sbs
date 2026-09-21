#include <ebml/EbmlDate.h>
#include <ebml/EbmlElement.h>
#include <ebml/EbmlFloat.h>
#include <ebml/EbmlMaster.h>
#include <ebml/EbmlSInteger.h>
#include <ebml/EbmlStream.h>
#include <ebml/EbmlString.h>
#include <ebml/EbmlUInteger.h>
#include <ebml/EbmlUnicodeString.h>
#include <ebml/StdIOCallback.h>

#include <matroska/KaxContexts.h>

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

static std::string formatId(const libebml::EbmlId& id)
{
    std::ostringstream out;

    // GetLength() liefert size_t, std::setw() erwartet int.
    const int width =
        static_cast<int>(id.GetLength() * std::size_t{2});

    out << "0x"
        << std::uppercase
        << std::hex
        << std::setfill('0')
        << std::setw(width)
        << id.GetValue();

    return out.str();
}


static void printIndent(unsigned int depth)
{
    for (unsigned int i = 0; i < depth; ++i)
        std::cout << "  ";
}


static void printValue(const libebml::EbmlElement& element)
{
    if (const auto* value =
            dynamic_cast<const libebml::EbmlUInteger*>(&element))
    {
        std::cout << " = " << value->GetValue();
        return;
    }

    if (const auto* value =
            dynamic_cast<const libebml::EbmlSInteger*>(&element))
    {
        std::cout << " = " << value->GetValue();
        return;
    }

    if (const auto* value =
            dynamic_cast<const libebml::EbmlFloat*>(&element))
    {
        std::cout << " = " << value->GetValue();
        return;
    }

    if (const auto* value =
            dynamic_cast<const libebml::EbmlDate*>(&element))
    {
        std::cout << " = " << value->GetEpochDate();
        return;
    }

    if (const auto* value =
            dynamic_cast<const libebml::EbmlString*>(&element))
    {
        std::cout << " = \""
                  << value->GetValue()
                  << "\"";
        return;
    }

    if (const auto* value =
            dynamic_cast<const libebml::EbmlUnicodeString*>(&element))
    {
        std::cout << " = \""
                  << value->GetValueUTF8()
                  << "\"";
        return;
    }
}


static void dumpElement(
    const libebml::EbmlElement& element,
    unsigned int depth)
{
    const libebml::EbmlCallbacks& callbacks =
        element.Generic();

    printIndent(depth);

    std::cout
        << callbacks.GetName()
        << " ["
        << formatId(callbacks.ClassId())
        << "]"
        << " @ "
        << std::dec
        << element.GetElementPosition();

    if (element.IsFiniteSize())
    {
        std::cout
            << ", size="
            << element.GetSize();
    }
    else
    {
        std::cout
            << ", size=unknown";
    }

    if (!element.IsMaster())
        printValue(element);

    std::cout << '\n';

    const auto* master =
        dynamic_cast<const libebml::EbmlMaster*>(&element);

    if (master == nullptr)
        return;

    for (const auto* child : *master)
    {
        if (child != nullptr)
            dumpElement(*child, depth + 1);
    }
}


static libebml::EbmlElement* readNextRootElement(
    libebml::EbmlStream& stream,
    size_t maxDataSize,
    int& upperLevel)
{
    /*
     * KaxMatroska_Context beschreibt die oberste Matroska-Ebene:
     *
     *   EBML
     *   Segment
     *
     * Der Context ist in KaxContexts.cpp definiert.
     */
    return stream.FindNextElement(
        libmatroska::KaxMatroska_Context,
        upperLevel,
        maxDataSize,
        false);
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr
            << "Usage: "
            << argv[0]
            << " <file.mkv>\n";

        return 1;
    }

    const char* filename = argv[1];

    try
    {
        /*
         * libebml erwartet einen IOCallback.
         *
         * std::ifstream kann hier NICHT direkt verwendet werden.
         */
        libebml::StdIOCallback file(
            filename,
            MODE_READ);

        libebml::EbmlStream stream(file);

        /*
         * Dateigröße bestimmen.
         *
         * IOCallback besitzt keinen getFileSize(), aber
         * setFilePointer()/getFilePointer() sind Teil der API.
         */
        file.setFilePointer(
            0,
            libebml::seek_end);

        const size_t fileSize =
            file.getFilePointer();

        file.setFilePointer(
            0,
            libebml::seek_beginning);

        /*
         * Die oberste Matroska-Kontextdefinition kennt EBML Header
         * und Segment.
         */
        int upperLevel = 0;

        while (true)
        {
            libebml::EbmlElement* element =
                readNextRootElement(
                    stream,
                    fileSize,
                    upperLevel);

            if (element == nullptr)
                break;

            /*
             * EbmlElement::Read() verwendet den Context des konkreten
             * Elements. Bei EbmlMaster führt das zum rekursiven Lesen
             * aller Kinder.
             *
             * Genau diese Vorgehensweise verwendet auch
             * EbmlMaster::Read() intern für seine Kinder.
             */
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

            delete element;

            /*
             * Bei einem gefundenen Element auf einer höheren Ebene
             * kann die Bibliothek ein Element in foundElement ablegen.
             *
             * Für die oberste Ebene erwarten wir normalerweise 0.
             * Sollte trotzdem eines zurückgegeben werden, darf es
             * nicht verloren gehen.
             */
            if (foundElement != nullptr)
            {
                delete foundElement;
                foundElement = nullptr;
            }

            /*
             * Für einen normalen MKV-Container ist nach EBML Header
             * und Segment das Ende der Datei erreicht.
             */
            if (file.getFilePointer() >= fileSize)
                break;

            /*
             * FindNextElement arbeitet mit dem aktuellen Dateizeiger.
             */
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
            << '\n';

        return 1;
    }

    return 0;
}
