#include "h264converter.h"

H264Converter::H264Converter(const std::string& input_file_name, const TrackInfo& info) :
	TrackConverter(input_file_name, info)
{
}

H264Converter::~H264Converter(void)
{
}

const std::string H264Converter::output_extension(void) const
{
	return "." + m_track_info.language + ".264";
}

size_t H264Converter::process_data(const std::vector<uint8_t>& data, const FrameInfo& info)
{
	(void)info;
	return data.size();

// Information	Benötigt?	Zweck
// H.264 Frame-Daten	Ja	eigentliche Videodaten
// PTS	Ja	Präsentationszeitpunkt
// DTS	Ja bzw. ableitbar	Decodierreihenfolge bei B-Frames
// Dauer	Ja, wenn verfügbar	zeitliche Länge des Frames
// Keyframe/RAP	Ja	Decodier-/Seek-Punkt
// TrackNumber/TrackUID	ggf.	Zuordnung zur Spur
// CodecPrivate	Ja	H.264 Initialisierungsdaten
// Codec ID	Ja	muss V_MPEG4/ISO/AVC sein
// Auflösung	ggf.	Stream-/Decoder-Konfiguration
// Codec Delay	ggf.	korrekte Zeitbasis
// ReferenceBlock	ggf.	Abhängigkeiten zwischen Frames
// DiscardPadding	ggf.	Randzeiten korrekt behandeln

// Wenn du möchtest, kann ich dir als Nächstes auch an einem konkreten Matroska-SimpleBlock
// Byte für Byte zeigen, wie du aus Cluster Timestamp + Block Timestamp + CodecPrivate + H.264 Payload
// einen korrekten H.264-Stream erzeugst – einschließlich der Behandlung von B-Frames, PTS/DTS und SPS/PPS.
}
