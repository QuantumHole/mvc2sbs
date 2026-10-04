# SPDX-FileCopyrightText: 2025, 2026 QuantumHole <QuantumHole@github.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

LICENSE_CODE = "GPL-2.0-or-later"
LICENSE_IMAGE = "CC-BY-SA-3.0-DE"
COPYRIGHT = "QuantumHole <QuantumHole@github.com>"

license-lint:
	reuse lint

license-annotate:
	reuse annotate --merge-copyrights --year $(shell date "+%Y") --copyright $(COPYRIGHT) --license $(LICENSE_CODE) Makefile README.md .gitignore common/*.mk
	reuse annotate --merge-copyrights --year $(shell date "+%Y") --copyright $(COPYRIGHT) --license $(LICENSE_CODE) --style cpp source/* source/*
	reuse annotate --merge-copyrights --year $(shell date "+%Y") --copyright $(COPYRIGHT) --license $(LICENSE_CODE) --style python common/*.cfg
	# reuse annotate --merge-copyrights --year $(shell date "+%Y") --copyright $(COPYRIGHT) --license $(LICENSE_IMAGE) images/*.svg
	-reuse download $(LICENSE_CODE) $(LICENSE_IMAGE)
