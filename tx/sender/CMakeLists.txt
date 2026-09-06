cmake_minimum_required(VERSION 3.10)
project(sender)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

include_directories(${Protobuf_INCLUDE_DIRS} include generated third_party/longhair)

add_executable(sender
    src/main.cpp
    src/sender.cpp
    src/reed_solomon.cpp
    src/file_reader.cpp
    src/hasher.cpp
    src/block_builder.cpp
    src/crc32.cpp
    src/interleaver.cpp
    src/pipeline.cpp
    third_party/longhair/cauchy_256.cpp
    third_party/longhair/gf256.cpp
    third_party/longhair/SiameseTools.cpp
    generated/uniflow.pb.cc
)


target_compile_options(sender PRIVATE -mssse3)
target_link_libraries(sender ${Protobuf_LIBRARIES} OpenSSL::Crypto ZLIB::ZLIB)