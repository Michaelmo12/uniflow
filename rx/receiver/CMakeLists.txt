cmake_minimum_required(VERSION 3.10)
project(receiver)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")

find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)

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

include_directories(${Protobuf_INCLUDE_DIRS} include vendor ${GENERATED_DIR})

add_executable(receiver
    src/receiver.cpp
    src/utils.cpp
    src/fec_manager.cpp
    src/io_managers.cpp
    vendor/cauchy_256.cpp
    vendor/gf256.cpp
    vendor/SiameseTools.cpp
    ${PROTO_SRC}
)

target_link_libraries(receiver ${Protobuf_LIBRARIES} Threads::Threads)
