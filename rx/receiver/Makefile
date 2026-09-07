CXX      := g++
CXXFLAGS := -std=c++17 -O3 -march=native -Wall -I./include -I./vendor -I./generated
LDFLAGS  := -lprotobuf -pthread

BUILD_DIR := build
GEN_DIR   := generated

PROTO     := uniflow.proto
PROTO_SRC := $(GEN_DIR)/uniflow.pb.cc
PROTO_HDR := $(GEN_DIR)/uniflow.pb.h
PROTO_OBJ := $(BUILD_DIR)/uniflow.pb.o

SRCS := src/receiver.cpp src/utils.cpp src/fec_manager.cpp src/io_managers.cpp
OBJS := $(patsubst src/%.cpp,$(BUILD_DIR)/src/%.o,$(SRCS))

VENDOR_SRCS := vendor/cauchy_256.cpp vendor/gf256.cpp vendor/SiameseTools.cpp
VENDOR_OBJS := $(patsubst vendor/%.cpp,$(BUILD_DIR)/vendor/%.o,$(VENDOR_SRCS))

ALL_OBJS := $(PROTO_OBJ) $(OBJS) $(VENDOR_OBJS)

TARGET := $(BUILD_DIR)/receiver

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(ALL_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(ALL_OBJS) $(LDFLAGS)

# One `protoc` invocation produces BOTH files, but a naive
# `$(PROTO_SRC) $(PROTO_HDR): $(PROTO)` rule with a single shared recipe is
# a classic `make -j` trap: make doesn't know one run satisfies both
$(PROTO_SRC): $(PROTO) | $(GEN_DIR)
	protoc --cpp_out=$(GEN_DIR) $(PROTO)

$(PROTO_HDR): $(PROTO_SRC)
	@true

$(PROTO_OBJ): $(PROTO_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/src/%.o: src/%.cpp $(PROTO_HDR) | $(BUILD_DIR)/src
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/vendor/%.o: vendor/%.cpp $(PROTO_HDR) | $(BUILD_DIR)/vendor
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(GEN_DIR) $(BUILD_DIR) $(BUILD_DIR)/src $(BUILD_DIR)/vendor:
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR) $(GEN_DIR)
