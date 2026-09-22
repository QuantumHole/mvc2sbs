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
#include <stdexcept>
#include <string>


struct PgsTrack
{
    std::uint64_t number = 0;
    std::string codecId = "";
};

struct PgsOutput
{
    std::ofstream file = {};
    std::uint64_t blockCount = 0;
    std::uint64_t segmentCount = 0;
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
    const auto* master =
        dynamic_cast<const libebml::EbmlMaster*>(&trackEntry);

    if (master == nullptr)
        return false;

    for (const auto* child : *master)
    {
        if (child == nullptr ||
            !isElement(*child, EBML_ID(libmatroska::KaxTrackNumber)))
            continue;

        const auto* value =
            dynamic_cast<const libmatroska::KaxTrackNumber*>(child);

        if (value == nullptr)
            return false;

        number = value->GetValue();
        return true;
    }

    return false;
}


static std::string getCodecId(
    const libmatroska::KaxTrackEntry& trackEntry)
{
    const auto* master =
        dynamic_cast<const libebml::EbmlMaster*>(&trackEntry);

    if (master == nullptr)
        return {};

    for (const auto* child : *master)
    {
        if (child == nullptr ||
            !isElement(*child, EBML_ID(libmatroska::KaxCodecID)))
            continue;

        const auto* value =
            dynamic_cast<const libmatroska::KaxCodecID*>(child);

        if (value != nullptr)
            return value->GetValue();
    }

    return {};
}


static std::string makeSupFilename(
    const std::string& inputFilename,
    std::uint64_t trackNumber)
{
    const std::size_t slash =
        inputFilename.find_last_of("/\\");

    const std::size_t start =
        slash == std::string::npos ? 0 : slash + 1;

    const std::size_t dot =
        inputFilename.find_last_of('.');

    const std::size_t ext =
        dot != std::string::npos && dot >= start
            ? dot
            : inputFilename.size();

    const std::string directory =
        slash == std::string::npos
            ? std::string()
            : inputFilename.substr(0, slash + 1);

    return directory +
           inputFilename.substr(start, ext - start) +
           "_track" +
           std::to_string(trackNumber) +
           ".sup";
}


static void openPgsOutputs(
    const std::string& inputFilename,
    const std::map<std::uint64_t, PgsTrack>& tracks,
    std::map<std::uint64_t, PgsOutput>& outputs)
{
    for (const auto& entry : tracks)
    {
        const auto trackNumber = entry.first;
        const auto filename =
            makeSupFilename(inputFilename, trackNumber);

        PgsOutput output;

        output.file.open(
            filename,
            std::ios::binary |
            std::ios::out |
            std::ios::trunc);

        if (!output.file)
            throw std::runtime_error(
                "Kann Datei nicht öffnen: " + filename);

        std::cout
            << "Track "
            << trackNumber
            << " -> "
            << filename
            << '\n';

        outputs.emplace(
            trackNumber,
            std::move(output));
    }
}


static void writeUint32BE(
    std::ofstream& file,
    std::uint32_t value)
{
    const unsigned char buffer[4] =
    {
        static_cast<unsigned char>((value >> 24) & 0xff),
        static_cast<unsigned char>((value >> 16) & 0xff),
        static_cast<unsigned char>((value >> 8) & 0xff),
        static_cast<unsigned char>(value & 0xff)
    };

    file.write(
        reinterpret_cast<const char*>(buffer),
        sizeof(buffer));
}


/*
 * Matroska-Timecode-Ticks -> PGS 90-kHz-PTS.
 *
 * timecodeScale ist Nanosekunden pro Matroska-Tick.
 */
static std::uint32_t matroskaTimecodeToPgsPts(
    std::int64_t timecode,
    std::uint64_t timecodeScale)
{
    if (timecode <= 0 || timecodeScale == 0)
        return 0;

    const std::uint64_t ticks =
        static_cast<std::uint64_t>(timecode);

    const std::uint64_t ns =
        ticks * timecodeScale;

    const std::uint64_t pts =
        (ns * 90000ULL) / 1000000000ULL;

    return static_cast<std::uint32_t>(
        pts & 0xffffffffULL);
}


static bool writePgsSupSegments(
    PgsOutput& output,
    const unsigned char* data,
    std::size_t size,
    std::uint32_t pts)
{
    if (data == nullptr || size == 0)
        return false;

    std::size_t offset = 0;
    bool wroteAnything = false;

    while (offset < size)
    {
        if (size - offset < 3)
        {
            std::cerr
                << "    Ungültiges PGS-Segment\n";
            return false;
        }

        const std::uint8_t segmentType =
            data[offset];

        const std::uint16_t segmentLength = static_cast<uint16_t>(
            (static_cast<std::uint16_t>(data[offset + 1]) << 8) |
             static_cast<std::uint16_t>(data[offset + 2]));

        const std::size_t segmentSize =
            3 + static_cast<std::size_t>(segmentLength);

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
                << '\n';

            return false;
        }

        const unsigned char magic[2] =
        {
            0x50,
            0x47
        };

        output.file.write(
            reinterpret_cast<const char*>(magic),
            sizeof(magic));

        writeUint32BE(output.file, pts);
        writeUint32BE(output.file, 0);

        output.file.write(
            reinterpret_cast<const char*>(data + offset),
            static_cast<std::streamsize>(segmentSize));

        if (!output.file)
            throw std::runtime_error(
                "Fehler beim Schreiben der SUP-Datei");

        output.byteCount +=
            10 + segmentSize;

        ++output.segmentCount;

        wroteAnything = true;
        offset += segmentSize;
    }

    return wroteAnything;
}


static void extractPgsBlock(
    libmatroska::KaxInternalBlock& block,
    const std::map<std::uint64_t, PgsTrack>& tracks,
    std::map<std::uint64_t, PgsOutput>& outputs,
    std::uint64_t clusterTimecode,
    std::uint64_t timecodeScale)
{
    const std::uint64_t trackNumber =
        block.TrackNum();

    if (tracks.find(trackNumber) == tracks.end())
        return;

    const auto outputIt =
        outputs.find(trackNumber);

    if (outputIt == outputs.end())
        return;

    PgsOutput& output =
        outputIt->second;

    /*
     * GetRelativeTimestamp() liefert den unskalierten
     * relativen Block-Timecode.
     */
    const std::int64_t relativeTimecode =
        block.GetRelativeTimestamp();

    const std::int64_t absoluteTimecode =
        static_cast<std::int64_t>(clusterTimecode) +
        relativeTimecode;

    const std::uint32_t pts =
        matroskaTimecodeToPgsPts(
            absoluteTimecode,
            timecodeScale);

    ++output.blockCount;

    for (unsigned int frameNumber = 0;
         frameNumber < block.NumberFrames();
         ++frameNumber)
    {
        libmatroska::DataBuffer& data =
            block.GetBuffer(frameNumber);

        const std::size_t frameSize =
            data.Size();

        if (data.Buffer() == nullptr || frameSize == 0)
        {
            std::cerr
                << "    Leerer PGS-Frame in Track "
                << trackNumber
                << '\n';

            continue;
        }

        const auto* buffer =
            reinterpret_cast<const unsigned char*>(
                data.Buffer());

        writePgsSupSegments(
            output,
            buffer,
            frameSize,
            pts);
    }
}


static bool readBlock(
    libmatroska::KaxInternalBlock& block,
    libebml::EbmlStream& stream,
    const std::map<std::uint64_t, PgsTrack>& tracks,
    std::map<std::uint64_t, PgsOutput>& outputs,
    std::uint64_t clusterTimecode,
    std::uint64_t timecodeScale)
{
    block.ReadData(
        stream.I_O(),
        libebml::SCOPE_ALL_DATA);

    const std::uint64_t trackNumber =
        block.TrackNum();

    if (tracks.find(trackNumber) == tracks.end())
        return true;

    extractPgsBlock(
        block,
        tracks,
        outputs,
        clusterTimecode,
        timecodeScale);

    return true;
}


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

    delete foundElement;

    const auto* master =
        dynamic_cast<const libebml::EbmlMaster*>(
            &tracksElement);

    if (master == nullptr)
        return;

    for (const auto* child : *master)
    {
        if (child == nullptr ||
            !isElement(
                *child,
                EBML_ID(libmatroska::KaxTrackEntry)))
            continue;

        const auto* entry =
            dynamic_cast<const libmatroska::KaxTrackEntry*>(
                child);

        if (entry == nullptr)
            continue;

        if (getCodecId(*entry) != "S_HDMV/PGS")
            continue;

        std::uint64_t number = 0;

        if (!getTrackNumber(*entry, number))
            continue;

        pgsTracks[number] =
        {
            number,
            "S_HDMV/PGS"
        };

        std::cout
            << "PGS Track "
            << number
            << '\n';
    }
}


static std::uint64_t readTimecodeScale(
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

    const auto* master =
        dynamic_cast<const libebml::EbmlMaster*>(&info);

    if (master == nullptr)
        return 1000000ULL;

    for (const auto* child : *master)
    {
        if (child == nullptr ||
            !isElement(
                *child,
                EBML_ID(libmatroska::KaxTimecodeScale)))
            continue;

        const auto* scale =
            dynamic_cast<const libmatroska::KaxTimecodeScale*>(
                child);

        if (scale != nullptr)
            return scale->GetValue();
    }

    return 1000000ULL;
}


static std::uint64_t getClusterEnd(
    const libebml::EbmlElement& cluster,
    std::uint64_t dataStart,
    std::size_t fileSize)
{
    if (!cluster.IsFiniteSize())
        return static_cast<std::uint64_t>(fileSize);

    const std::uint64_t size =
        cluster.GetSize();

    const std::uint64_t end =
        dataStart + size;

    return end > static_cast<std::uint64_t>(fileSize)
        ? static_cast<std::uint64_t>(fileSize)
        : end;
}


static void streamBlockGroup(
    libebml::EbmlStream& stream,
    const std::map<std::uint64_t, PgsTrack>& tracks,
    std::map<std::uint64_t, PgsOutput>& outputs,
    std::uint64_t groupEnd,
    std::size_t fileSize,
    std::uint64_t clusterTimecode,
    std::uint64_t timecodeScale)
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
            return;

        if (element->GetElementPosition() >= groupEnd)
        {
            delete element;
            return;
        }

        auto* block =
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
                    outputs,
                    clusterTimecode,
                    timecodeScale);
            }
            catch (...)
            {
                delete element;

                stream.I_O().setFilePointer(
                    static_cast<std::int64_t>(groupEnd),
                    libebml::seek_beginning);

                return;
            }

            delete element;
            continue;
        }

        try
        {
            element->SkipData(
                stream,
                element->Generic().Context);
        }
        catch (...)
        {
            delete element;

            stream.I_O().setFilePointer(
                static_cast<std::int64_t>(groupEnd),
                libebml::seek_beginning);

            return;
        }

        delete element;
    }
}


static void streamCluster(
    libebml::EbmlStream& stream,
    const std::map<std::uint64_t, PgsTrack>& tracks,
    std::map<std::uint64_t, PgsOutput>& outputs,
    std::size_t fileSize,
    std::uint64_t clusterEnd,
    std::uint64_t timecodeScale)
{
    int upperLevel = 0;
    std::uint64_t clusterTimecode = 0;
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
            stream.I_O().setFilePointer(
                static_cast<std::int64_t>(clusterEnd),
                libebml::seek_beginning);

            return;
        }

        if (element->GetElementPosition() >= clusterEnd)
        {
            delete element;
            break;
        }

        if (isElement(
                *element,
                EBML_ID(libmatroska::KaxClusterTimecode)))
        {
            auto* timecode =
                dynamic_cast<libmatroska::KaxClusterTimecode*>(
                    element);

            if (timecode != nullptr)
            {
                timecode->ReadData(
                    stream.I_O(),
                    libebml::SCOPE_ALL_DATA);

                clusterTimecode =
                    timecode->GetValue();

                haveClusterTimecode = true;
            }

            delete element;
            continue;
        }

        auto* block =
            dynamic_cast<libmatroska::KaxInternalBlock*>(
                element);

        if (block != nullptr)
        {
            const std::uint64_t dataStart =
                stream.I_O().getFilePointer();

            const std::uint64_t blockSize =
                block->GetSize();

            if (block->IsFiniteSize() &&
                (dataStart > clusterEnd ||
                 blockSize > clusterEnd - dataStart))
            {
                delete element;

                stream.I_O().setFilePointer(
                    static_cast<std::int64_t>(clusterEnd),
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
                        tracks,
                        outputs,
                        clusterTimecode,
                        timecodeScale);
                }
                else
                {
                    block->ReadData(
                        stream.I_O(),
                        libebml::SCOPE_ALL_DATA);
                }
            }
            catch (...)
            {
                delete element;

                stream.I_O().setFilePointer(
                    static_cast<std::int64_t>(clusterEnd),
                    libebml::seek_beginning);

                return;
            }

            delete element;
            continue;
        }

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
                    delete element;

                    stream.I_O().setFilePointer(
                        static_cast<std::int64_t>(clusterEnd),
                        libebml::seek_beginning);

                    return;
                }

                groupEnd =
                    groupDataStart + groupSize;
            }

            streamBlockGroup(
                stream,
                tracks,
                outputs,
                groupEnd,
                fileSize,
                clusterTimecode,
                timecodeScale);

            delete element;
            continue;
        }

        const std::uint64_t dataStart =
            stream.I_O().getFilePointer();

        if (element->IsFiniteSize())
        {
            const std::uint64_t size =
                element->GetSize();

            if (dataStart > clusterEnd ||
                size > clusterEnd - dataStart)
            {
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
        catch (...)
        {
            delete element;

            stream.I_O().setFilePointer(
                static_cast<std::int64_t>(clusterEnd),
                libebml::seek_beginning);

            return;
        }

        delete element;
    }
}


static void streamSegment(
    libebml::EbmlStream& stream,
    const std::string& filename,
    std::map<std::uint64_t, PgsTrack>& pgsTracks,
    std::map<std::uint64_t, PgsOutput>& pgsOutputs,
    std::size_t fileSize)
{
    int upperLevel = 0;
    std::uint64_t timecodeScale = 1000000ULL;

    while (true)
    {
        libebml::EbmlElement* element =
            stream.FindNextElement(
                libmatroska::KaxSegment_Context,
                upperLevel,
                fileSize,
                false);

        if (element == nullptr)
            return;

        if (isElement(
                *element,
                EBML_ID(libmatroska::KaxInfo)))
        {
            auto* info =
                dynamic_cast<libmatroska::KaxInfo*>(
                    element);

            if (info != nullptr)
            {
                timecodeScale =
                    readTimecodeScale(
                        *info,
                        stream);

                std::cout
                    << "TimecodeScale: "
                    << timecodeScale
                    << " ns\n";
            }

            delete element;
            continue;
        }

        if (isElement(
                *element,
                EBML_ID(libmatroska::KaxTracks)))
        {
            auto* tracks =
                dynamic_cast<libmatroska::KaxTracks*>(
                    element);

            if (tracks != nullptr)
            {
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

        if (isElement(
                *element,
                EBML_ID(libmatroska::KaxCluster)))
        {
            const std::uint64_t dataStart =
                stream.I_O().getFilePointer();

            const std::uint64_t clusterEnd =
                getClusterEnd(
                    *element,
                    dataStart,
                    fileSize);

            streamCluster(
                stream,
                pgsTracks,
                pgsOutputs,
                fileSize,
                clusterEnd,
                timecodeScale);

            delete element;
            continue;
        }

        try
        {
            element->SkipData(
                stream,
                element->Generic().Context);
        }
        catch (...)
        {
            delete element;
            return;
        }

        delete element;
    }
}


int main(int argc, const char** argv)
{
    if (argc != 2)
    {
        std::cerr
            << "Usage: "
            << argv[0]
            << " <file.mkv>\n";

        return 1;
    }

    const std::string filename = argv[1];

    std::map<std::uint64_t, PgsTrack> pgsTracks;
    std::map<std::uint64_t, PgsOutput> pgsOutputs;

    try
    {
        libebml::StdIOCallback file(
            filename.c_str(),
            MODE_READ);

        libebml::EbmlStream stream(file);

        file.setFilePointer(
            0,
            libebml::seek_end);

        const std::size_t fileSize =
            file.getFilePointer();

        file.setFilePointer(
            0,
            libebml::seek_beginning);

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
                break;

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

            if (isElement(
                    *element,
                    EBML_ID(libmatroska::KaxSegment)))
            {
                streamSegment(
                    stream,
                    filename,
                    pgsTracks,
                    pgsOutputs,
                    fileSize);

                delete element;
                break;
            }

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
            << "Fehler: "
            << e.what()
            << '\n';

        return 1;
    }

    for (auto& entry : pgsOutputs)
    {
        PgsOutput& output = entry.second;

        output.file.flush();
        output.file.close();

        std::cout
            << "Track "
            << entry.first
            << ": "
            << output.blockCount
            << " Blocks, "
            << output.segmentCount
            << " Segmente, "
            << output.byteCount
            << " Bytes\n";
    }

    return 0;
}
