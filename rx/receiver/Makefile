CXX      := g++
CXXFLAGS := -std=c++17 -O3 -march=native -Wall -I./include -I./vendor -I.
LDFLAGS  := -lprotobuf -pthread

PROTO     := uniflow.proto
PROTO_SRC := uniflow.pb.cc
PROTO_HDR := uniflow.pb.h
PROTO_OBJ := uniflow.pb.o

SRCS := src/receiver.cpp src/utils.cpp src/fec_manager.cpp src/io_managers.cpp
OBJS := $(SRCS:.cpp=.o)

VENDOR_SRCS := vendor/cauchy_256.cpp vendor/gf256.cpp vendor/SiameseTools.cpp
VENDOR_OBJS := $(VENDOR_SRCS:.cpp=.o)

ALL_OBJS := $(PROTO_OBJ) $(OBJS) $(VENDOR_OBJS)

TARGET := receiver

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(ALL_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(ALL_OBJS) $(LDFLAGS)

# One `protoc` invocation produces BOTH files, but a naive
# `$(PROTO_SRC) $(PROTO_HDR): $(PROTO)` rule with a single shared recipe is
# a classic `make -j` trap: make doesn't know one run satisfies both
$(PROTO_SRC): $(PROTO)
	protoc --cpp_out=. $(PROTO)

$(PROTO_HDR): $(PROTO_SRC)
	@true

$(PROTO_OBJ): $(PROTO_SRC)
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/%.o: src/%.cpp $(PROTO_HDR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

vendor/%.o: vendor/%.cpp $(PROTO_HDR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(ALL_OBJS) $(PROTO_SRC) $(PROTO_HDR)
