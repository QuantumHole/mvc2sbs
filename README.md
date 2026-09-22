<!--
SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>

SPDX-License-Identifier: GPL-3.0-or-later
-->

mkvstreams is a tool for extracting PGS subtitles from MKV files.

# Dependencies

## Runtime Dependencies
* [libebml](https://github.com/Matroska-Org/libebml)
* [libmatroska](https://github.com/Matroska-Org/libmatroska)

## Build Dependencies

* [gcc](https://gcc.gnu.org) or [clang](https://clang.llvm.org)
* [GNU make](https://www.gnu.org/software/make)
* [pkg-config](https://www.freedesktop.org/wiki/Software/pkg-config)

## Lint Dependencies

* [uncrustify](https://github.com/uncrustify/uncrustify)
* [reuse](https://reuse.software)

# Building

`make -j all`

# Contribution

Before committing, please take care of source code format and proper license information.

`make lint`

# Background Information

Some useful resources for learning about the topics covered in this project.
* Matroska file format [Matroska Media Container](https://www.matroska.org)

