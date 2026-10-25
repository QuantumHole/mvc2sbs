#ifndef COPYCONVERTER_H
#define COPYCONVERTER_H

#include "trackconverter.h"

/** trivial converter that just copies all input bytes into the output stream. */
class CopyConverter : public TrackConverter
{
	private:
		/** process packets of data.
		 * @param data one bunch of data to be processed. Data may be cut at arbitrary positions into packets.
		 * @param info meta data for the current frame, that may be contained in the current packet of data.
		 * @return number of processed bytes. This must be the size of data, everything else is considered an error.
		 */
		virtual size_t process_data(const std::vector<uint8_t>& data, const FrameInfo& info) override;

	public:
		/** constructor.
		 * @param trackinfo information about the assigned track.
		 */
		explicit CopyConverter(const TrackInfo& trackinfo);
};

#endif
