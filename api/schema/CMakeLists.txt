kearne_require_protobuf()
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

set(proto_root "${CMAKE_CURRENT_SOURCE_DIR}/proto")
file(GLOB_RECURSE proto_files CONFIGURE_DEPENDS "${proto_root}/*.proto")
set(generated_root "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(generated_cpp "${generated_root}/cpp")
set(generated_python "${generated_root}/python")
set(generated_python_package "${generated_python}/kearne/api/v1")
set_property(GLOBAL PROPERTY KEARNE_API_GENERATED_PYTHON_ROOT
    "${generated_python}")
set(generated_python_init_files
    "${generated_python}/kearne/__init__.py"
    "${generated_python}/kearne/api/__init__.py"
    "${generated_python_package}/__init__.py")
set(descriptor "${generated_root}/kearne-api.pb")
set(generated_sources)
set(generated_headers)
set(generated_python_files)
foreach(proto IN LISTS proto_files)
    file(RELATIVE_PATH relative "${proto_root}" "${proto}")
    string(REGEX REPLACE "\\.proto$" ".pb.cc" cpp_source "${relative}")
    string(REGEX REPLACE "\\.proto$" ".pb.h" cpp_header "${relative}")
    string(REGEX REPLACE "\\.proto$" "_pb2.py" python_source "${relative}")
    list(APPEND generated_sources "${generated_cpp}/${cpp_source}")
    list(APPEND generated_headers "${generated_cpp}/${cpp_header}")
    list(APPEND generated_python_files "${generated_python}/${python_source}")
endforeach()

add_custom_command(
    OUTPUT ${generated_sources} ${generated_headers}
           ${generated_python_files} ${generated_python_init_files}
           "${descriptor}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${generated_cpp}" "${generated_python_package}"
    COMMAND Kearne::Protoc
            "--proto_path=${proto_root}"
            "--proto_path=${KEARNE_PROTOBUF_INCLUDE_DIR}"
            "--cpp_out=${generated_cpp}"
            "--python_out=${generated_python}"
            "--descriptor_set_out=${descriptor}"
            --include_imports --retain_options
            ${proto_files}
    COMMAND "${CMAKE_COMMAND}" -E touch ${generated_python_init_files}
    DEPENDS Kearne::Protoc ${proto_files}
    VERBATIM)

add_library(kearne_api_wire STATIC
    ${generated_sources} ${generated_headers}
    src/strong_types.cpp
    src/wire_validation.cpp)
add_library(Kearne::ApiWire ALIAS kearne_api_wire)
target_include_directories(kearne_api_wire
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_include_directories(kearne_api_wire SYSTEM
    PUBLIC "$<BUILD_INTERFACE:${generated_cpp}>")
target_link_libraries(kearne_api_wire
    PUBLIC Kearne::Base protobuf::libprotobuf
    PRIVATE Kearne::BuildOptions)

set(metadata "${generated_root}/registry.json")
add_custom_command(
    OUTPUT "${metadata}"
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/schema_metadata.py"
            --descriptor "${descriptor}"
            --python-root "${generated_python}"
            --output "${metadata}"
    DEPENDS "${descriptor}" ${generated_python_files}
            ${generated_python_init_files}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/schema_metadata.py"
    VERBATIM)
add_custom_target(kearne_api_metadata ALL DEPENDS "${metadata}")

install(TARGETS kearne_api_wire EXPORT KearneTargets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY "${generated_cpp}/kearne/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/kearne"
        FILES_MATCHING PATTERN "*.pb.h")
install(FILES "${descriptor}" "${metadata}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/kearne/api")

if(BUILD_TESTING)
    add_executable(kearne_api_wire_contracts tests/wire_contracts.cpp)
    target_link_libraries(kearne_api_wire_contracts PRIVATE
        Kearne::ApiWire Kearne::BuildOptions)
    add_test(NAME api-wire-contracts COMMAND kearne_api_wire_contracts)
    add_library(kearne_api_wire_fuzz OBJECT tests/wire_fuzz.cpp)
    target_link_libraries(kearne_api_wire_fuzz PRIVATE
        Kearne::ApiWire Kearne::BuildOptions)
    add_executable(kearne_api_wire_fuzz_smoke
        tests/wire_fuzz_smoke.cpp $<TARGET_OBJECTS:kearne_api_wire_fuzz>)
    target_link_libraries(kearne_api_wire_fuzz_smoke PRIVATE
        Kearne::ApiWire Kearne::Testkit Kearne::BuildOptions
        absl::log_globals absl::log_initialize)
    add_test(NAME api-wire-fuzz-smoke COMMAND kearne_api_wire_fuzz_smoke)
    add_test(
        NAME api-schema-contract
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/schema_contract.py"
                --descriptor "${descriptor}"
                --metadata "${metadata}"
                --generator "${CMAKE_CURRENT_SOURCE_DIR}/tools/schema_metadata.py"
                --python-root "${generated_python}"
                --protoc "$<TARGET_FILE:Kearne::Protoc>"
                --lock "${PROJECT_SOURCE_DIR}/dependencies.lock.json")
endif()
