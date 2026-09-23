# SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

PROJECT_ROOT := .
DEPENDENCIES = "libmatroska libebml"

include common/cplusplus.mk
include common/license.mk
include common/makefile.mk
include common/doxygen.mk

TARGET   := mkvmvc2sbs

OBJECTS := \
	$(BUILD_DIR)/$(TARGET).o \
	$(BUILD_DIR)/mkvstreamer.o \

.PHONY: all

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@echo "[$(LD)] $@"
	@$(CXX) $(LDFLAGS) -o $@ $^ -s $(LDLIBS)
