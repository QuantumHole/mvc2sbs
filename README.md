<!--
SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>

SPDX-License-Identifier: GPL-2.0-or-later
-->

# <img src="images/mvc2sbs.svg" alt="MVC -> SBS" width="20%" />

mkvmvc2sbs is a tool for converting MKV files with MVC video into SBS format.
The video stream is converted from MVC to SBS format. This doubles the video dimension horizontally.
Subtitles in PGS/SUP format are extracted and saved as PGS/SUP files.
As PGS/SUP is an image format, their horizontal dimensions are doubled as well.
Additionally, subtitles in supported languages are converted into
text via optical character recognition (OCR).
Recognized subtitles are saved in SubRip (SRT) format.

Converted tracks are saved beside the original file with the appropriate file extensions
suitable for the contained track format.

The saved tracks can be merged into a single MKV file, e.g. with mkvtoolnix.
This last step of re-muxing should eventually also be performed automatically.
Nevertheless, this has not yet been implemented.

# Dependencies

## Runtime Dependencies

The Converter requires the following libraries to be installed at runtime.
* [libebml](https://github.com/Matroska-Org/libebml)
* [libmatroska](https://github.com/Matroska-Org/libmatroska)
* [Tesseract](https://tesseractocr.org/)
* [libpng](https://www.libpng.org/pub/png/libpng.html)
* [libavcodec libavutil](https://ffmpeg.org)

## Build Dependencies

* [gcc](https://gcc.gnu.org) or [clang](https://clang.llvm.org)
* [GNU make](https://www.gnu.org/software/make)
* [pkg-config](https://www.freedesktop.org/wiki/Software/pkg-config)
* [edge264-mvc](https://github.com/jens-duttke/edge264-mvc) with one additional function to reach timing information.
This library has been [forked](https://github.com/QuantumHole/edge264-mvc.git) to accommodate the neccessary function.

## Lint Dependencies

* [uncrustify](https://github.com/uncrustify/uncrustify) for fixing code style.
* [reuse](https://reuse.software) for managing license information in all files.
* [doxygen](https://www.doxygen.nl) for building the development documentation.

# Building

Prerequesites can be installed via the package manager of your distribution, e.g.

`pacman -S libebml libmatroska tesseract tesseract-data-eng tesseract-data-deu ...`

On **Arch Linux**, the recommended packages are
* [core/gcc](https://archlinux.org/packages/core/x86_64/gcc)
* [core/make](https://archlinux.org/packages/core/x86_64/make)
* [core/pkgconf](https://archlinux.org/packages/core/x86_64/pkgconf)
* [extra/clang](https://archlinux.org/packages/extra/x86_64/clang)
* [extra/doxygen](https://archlinux.org/packages/extra/x86_64/doxygen)
* [extra/ffmpeg](https://archlinux.org/packages/extra/x86_64/ffmpeg)
* [extra/libebml](https://archlinux.org/packages/extra/x86_64/libebml)
* [extra/libmatroska](https://archlinux.org/packages/extra/x86_64/matroska)
* [extra/libpng](https://archlinux.org/packages/extra/x86_64/libpng)
* [extra/reuse](https://archlinux.org/packages/extra/any/reuse)
* [extra/tesseract](https://archlinux.org/packages/extra/x86_64/tesseract)
* [extra/uncrustify](https://archlinux.org/packages/extra/x86_64/uncrustify)

Additional possible packages for OCR language support
* [extra/tesseract-data-deu](https://archlinux.org/packages/extra/any/tesseract-data-deu)
* [extra/tesseract-data-eng](https://archlinux.org/packages/extra/any/tesseract-data-eng)
* [extra/tesseract-data-fra](https://archlinux.org/packages/extra/any/tesseract-data-fra)

## Compilation

`make -j all`
For selection of compiler, the project can also be built with
`make -j gnu` (GCC) or `make -j llvm` (clang)

# Contribution

Before committing, please take care of source code format and proper license information.

`make lint`

# Background Information

Some useful resources for learning about the topics covered in this project.
* Matroska file format [Matroska Media Container](https://www.matroska.org)
* PGS explanation [Presentation Graphics Stream](https://blog.thescorpius.com/index.php/2017/07/15/presentation-graphic-stream-sup-files-bluray-subtitle-format/)
* SRT explanation [SubRip Subtitles](https://en.wikipedia.org/wiki/SubRip)
* Complete solution to record, convert and stream audio and video [FFmpeg](https://ffmpeg.org/)
* MKV editing suite [MKVToolNix](https://mkvtoolnix.download/downloads.html)
