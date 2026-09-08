cmake_minimum_required(VERSION 3.10)
project(sender)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

set(PROTO_ROOT ${CMAKE_SOURCE_DIR}/../../proto)
set(PROTO_FILE ${PROTO_ROOT}/uniflow.proto)
set(GENERATED_DIR ${CMAKE_SOURCE_DIR}/generated)
set(PROTO_SRC ${GENERATED_DIR}/uniflow.pb.cc)
set(PROTO_HDR ${GENERATED_DIR}/uniflow.pb.h)

add_custom_command(
    OUTPUT ${PROTO_SRC} ${PROTO_HDR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND protobuf::protoc --cpp_out=${GENERATED_DIR} -I ${PROTO_ROOT} ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating protobuf sources from ${PROTO_FILE}"
)

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
    ${PROTO_SRC}
)


target_compile_options(sender PRIVATE -mssse3)
target_link_libraries(sender ${Protobuf_LIBRARIES} OpenSSL::Crypto ZLIB::ZLIB)