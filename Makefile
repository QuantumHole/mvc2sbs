# SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

PROJECT_ROOT := .
DEPENDENCIES = "libmatroska libebml tesseract libpng"

include common/cplusplus.mk
include common/license.mk
include common/makefile.mk
include common/doxygen.mk

TARGET   := mkvmvc2sbs

OBJECTS := \
	$(BUILD_DIR)/$(TARGET).o \
	$(BUILD_DIR)/mkvstreamer.o \
	$(BUILD_DIR)/trackconverter.o \
	$(BUILD_DIR)/pgsconverter.o \
	$(BUILD_DIR)/pgsstream.o \
	$(BUILD_DIR)/ocr.o \
	$(BUILD_DIR)/h264converter.o \

.PHONY: all

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@echo "[$(LD)] $@"
	@$(CXX) $(LDFLAGS) -o $@ $^ -s $(LDLIBS)
