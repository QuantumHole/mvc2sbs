# SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

PROJECT_ROOT := .
DEPENDENCIES = "libmatroska libebml tesseract libpng libavcodec libavutil"

include common/cplusplus.mk
include common/license.mk
include common/makefile.mk
include common/doxygen.mk

TARGET   := mkvmvc2sbs

EDGE264_DIR := edge264-mvc
EDGE264_OBJECTS := \
	$(EDGE264_DIR)/edge264.o \
	$(EDGE264_DIR)/edge264_headers_log.o \

OBJECTS := \
	$(BUILD_DIR)/$(TARGET).o \
	$(BUILD_DIR)/mkvstreamer.o \
	$(BUILD_DIR)/trackconverter.o \
	$(BUILD_DIR)/pgsconverter.o \
	$(BUILD_DIR)/pgsstream.o \
	$(BUILD_DIR)/ocr.o \
	$(BUILD_DIR)/h264converter.o \
	$(BUILD_DIR)/mvc2sbsconverter.o \
	$(EDGE264_OBJECTS)

CXXFLAGS += -isystem edge264-mvc

.PHONY: all

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@echo "[$(LD)] $@"
	@$(CXX) $(LDFLAGS) -o $@ $^ -s $(LDLIBS)

$(EDGE264_OBJECTS):
	@echo "[LIB] $@"
	# @$(MAKE) --directory edge264-mvc clean
	@$(MAKE) --directory edge264-mvc -j all
