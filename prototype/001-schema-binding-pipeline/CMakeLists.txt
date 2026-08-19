cmake_minimum_required(VERSION 3.28)

project(kearne_schema_prototype LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core)

set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    absl
    URL https://github.com/abseil/abseil-cpp/archive/refs/tags/20250512.1.tar.gz
    URL_HASH SHA256=9b7a064305e9fd94d124ffa6cc358592eb42b5da588fb4e07d09254aa40086db
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(absl)

set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
set(protobuf_BUILD_LIBPROTOC ON CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    protobuf
    URL https://github.com/protocolbuffers/protobuf/releases/download/v35.1/protobuf-35.1.tar.gz
    URL_HASH SHA256=f0b6838e7522a8da96126d487068c959bc624926368f3024ac8fd03abd0a1ac4
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(protobuf)

set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PROTO_FILES
    "${PROTO_DIR}/options.proto"
    "${PROTO_DIR}/api.proto"
    "${PROTO_DIR}/evolution.proto"
)
set(GENERATED_CPP
    "${GENERATED_DIR}/options.pb.cc"
    "${GENERATED_DIR}/api.pb.cc"
    "${GENERATED_DIR}/evolution.pb.cc"
)
set(GENERATED_HEADERS
    "${GENERATED_DIR}/options.pb.h"
    "${GENERATED_DIR}/api.pb.h"
    "${GENERATED_DIR}/evolution.pb.h"
)
set(GENERATED_PYTHON
    "${GENERATED_DIR}/options_pb2.py"
    "${GENERATED_DIR}/api_pb2.py"
    "${GENERATED_DIR}/evolution_pb2.py"
)
set(DESCRIPTOR_SET "${GENERATED_DIR}/kearne-schema.pb")

add_custom_command(
    OUTPUT ${GENERATED_CPP} ${GENERATED_HEADERS} ${GENERATED_PYTHON} ${DESCRIPTOR_SET}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_DIR}"
    COMMAND protobuf::protoc
        "--proto_path=${PROTO_DIR}"
        "--proto_path=${protobuf_SOURCE_DIR}/src"
        "--cpp_out=${GENERATED_DIR}"
        "--python_out=${GENERATED_DIR}"
        "--descriptor_set_out=${DESCRIPTOR_SET}"
        --include_imports
        --include_source_info
        ${PROTO_FILES}
    DEPENDS protobuf::protoc ${PROTO_FILES}
    VERBATIM
)

add_library(kearne-prototype-schema STATIC ${GENERATED_CPP} ${GENERATED_HEADERS})
target_include_directories(kearne-prototype-schema SYSTEM PUBLIC "${GENERATED_DIR}")
target_link_libraries(kearne-prototype-schema PUBLIC protobuf::libprotobuf)

add_library(kearne-prototype-engine STATIC src/engine.cpp src/engine.hpp)
target_include_directories(kearne-prototype-engine PUBLIC src)
target_link_libraries(kearne-prototype-engine PUBLIC kearne-prototype-schema)

add_executable(kearne-schema-probe src/main.cpp)
target_link_libraries(kearne-schema-probe PRIVATE kearne-prototype-engine)

add_executable(kearne-schema-qt-adapter src/qt_adapter.cpp)
target_link_libraries(kearne-schema-qt-adapter PRIVATE kearne-prototype-engine Qt6::Core)

add_executable(kearne-schema-compiler src/schema_compiler.cpp)
target_link_libraries(kearne-schema-compiler PRIVATE kearne-prototype-schema)

set(SCHEMA_ARTIFACT_DIR "${GENERATED_DIR}/metadata")
set(SCHEMA_ARTIFACTS
    "${SCHEMA_ARTIFACT_DIR}/command-registry.json"
    "${SCHEMA_ARTIFACT_DIR}/project.rename.tool.json"
)
add_custom_command(
    OUTPUT ${SCHEMA_ARTIFACTS}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SCHEMA_ARTIFACT_DIR}"
    COMMAND kearne-schema-compiler --output "${SCHEMA_ARTIFACT_DIR}"
    DEPENDS kearne-schema-compiler
    VERBATIM
)
add_custom_target(kearne-schema-artifacts ALL DEPENDS ${SCHEMA_ARTIFACTS})

add_library(kearne-schema-fuzz-target OBJECT src/fuzz_target.cpp)
target_link_libraries(kearne-schema-fuzz-target PRIVATE kearne-prototype-engine)
add_executable(kearne-schema-fuzz-smoke
    src/fuzz_smoke.cpp
    $<TARGET_OBJECTS:kearne-schema-fuzz-target>
)
target_link_libraries(kearne-schema-fuzz-smoke PRIVATE kearne-prototype-engine)

function(kearne_prototype_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
endfunction()

foreach(target
    kearne-prototype-engine
    kearne-schema-probe
    kearne-schema-qt-adapter
    kearne-schema-compiler
    kearne-schema-fuzz-target
    kearne-schema-fuzz-smoke
)
    kearne_prototype_enable_warnings(${target})
endforeach()

enable_testing()
add_test(NAME schema.cpp COMMAND kearne-schema-probe --self-test)
add_test(NAME schema.qt-adapter COMMAND kearne-schema-qt-adapter)
add_test(NAME schema.fuzz-smoke COMMAND kearne-schema-fuzz-smoke --seed 104729 --cases 10000)
