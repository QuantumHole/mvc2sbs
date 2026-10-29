# SPDX-FileCopyrightText: 2026 QuantumHole <QuantumHole@github.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

PROJECT_ROOT := .
DEPENDENCIES = "libmatroska libebml tesseract libpng libavcodec libavutil libavformat"

include common/cplusplus.mk
include common/license.mk
include common/makefile.mk
include common/doxygen.mk

TARGET   := mkvmvc2sbs

EDGE264_DIR := edge264-mvc
EDGE264_OBJECTS := \
	$(EDGE264_DIR)/edge264mvc.o \
	$(EDGE264_DIR)/edge264mvc_headers_log.o \

OBJECTS := \
	$(BUILD_DIR)/$(TARGET).o \
	$(BUILD_DIR)/mkvstreamer.o \
	$(BUILD_DIR)/trackconverter.o \
	$(BUILD_DIR)/pgsconverter.o \
	$(BUILD_DIR)/pgsstream.o \
	$(BUILD_DIR)/ocr.o \
	$(BUILD_DIR)/h264converter.o \
	$(BUILD_DIR)/mvc2sbsconverter.o \
	$(BUILD_DIR)/trackmuxer.o \
	$(BUILD_DIR)/copyconverter.o \
	$(BUILD_DIR)/srtconverter.o \
	$(BUILD_DIR)/timetools.o \
	$(EDGE264_OBJECTS)

CXXFLAGS += -isystem edge264-mvc

.PHONY: all
.PHONY: check-pgs
.PHONY: check-srt-timestamps

all: $(TARGET)

check-pgs: $(BUILD_DIR)/pgsstream_composition_test
	@$<

check-srt-timestamps: $(BUILD_DIR)/srt_timestamp_test
	@$<

$(BUILD_DIR)/pgsstream_composition_test: source/tests/pgsstream_composition_test.cpp source/pgsstream.cpp | $(BUILD_DIR)
	@echo "[$(CXX)] PGS composition regression test"
	@$(CXX) $(filter-out -c,$(CXXFLAGS)) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/srt_timestamp_test: source/tests/srt_timestamp_test.cpp source/timetools.cpp source/timetools.h | $(BUILD_DIR)
	@echo "[$(CXX)] SRT timestamp regression test"
	@$(CXX) $(filter-out -c,$(CXXFLAGS)) $(LDFLAGS) -o $@ source/tests/srt_timestamp_test.cpp source/timetools.cpp $(LDLIBS)

$(TARGET): $(OBJECTS)
	@echo "[$(LD)] $@"
	@$(CXX) $(LDFLAGS) -o $@ $^ -s $(LDLIBS)

$(EDGE264_OBJECTS):
	@echo "[LIB] $@"
	# @$(MAKE) --directory edge264-mvc clean
	@$(MAKE) --directory edge264-mvc -j all
