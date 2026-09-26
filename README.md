<!--
SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>

SPDX-License-Identifier: GPL-3.0-or-later
-->

mkvstreams is a tool for extracting PGS subtitles from MKV files.
Subtitles are saved as PGS/SUP files.
Additionally, subtitles in supported languages are converted into
text via optical character recognition (OCR).
Recognized subtitles are saved in SubRip (SRT) format.

# Dependencies

## Runtime Dependencies
* [libebml](https://github.com/Matroska-Org/libebml)
* [libmatroska](https://github.com/Matroska-Org/libmatroska)
* [Tesseract](https://tesseractocr.org/)
* [libpng](https://www.libpng.org/pub/png/libpng.html)

## Build Dependencies

* [gcc](https://gcc.gnu.org) or [clang](https://clang.llvm.org)
* [GNU make](https://www.gnu.org/software/make)
* [pkg-config](https://www.freedesktop.org/wiki/Software/pkg-config)

## Lint Dependencies

* [uncrustify](https://github.com/uncrustify/uncrustify)
* [reuse](https://reuse.software)

# Building

Prerequesites can be installed via the package manager of your distribution, e.g.

`pacman -S libebml libmatroska tesseract tesseract-data-eng tesseract-data-deu ...`

## Compilation

`make -j all`

# Contribution

Before committing, please take care of source code format and proper license information.

`make lint`

# Background Information

Some useful resources for learning about the topics covered in this project.
* Matroska file format [Matroska Media Container](https://www.matroska.org)
* PGS explanation [Presentation Graphics Stream](https://blog.thescorpius.com/index.php/2017/07/15/presentation-graphic-stream-sup-files-bluray-subtitle-format/)
* SRT explanation [SubRip Subtitles](https://en.wikipedia.org/wiki/SubRip)
