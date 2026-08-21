cmake_minimum_required(VERSION 3.28)

project(Kearne VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
include(GNUInstallDirs)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(KearneDependencies)

option(KEARNE_BUILD_DESKTOP "Build the Qt desktop application" ON)
option(KEARNE_BUILD_API "Build the Engineering API schemas" ON)
option(KEARNE_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)

add_library(kearne_build_options INTERFACE)
add_library(Kearne::BuildOptions ALIAS kearne_build_options)
target_compile_features(kearne_build_options INTERFACE cxx_std_23)
if(MSVC)
    target_compile_options(kearne_build_options INTERFACE /W4 /permissive-)
else()
    target_compile_options(kearne_build_options INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(KEARNE_ENABLE_SANITIZERS)
        target_compile_options(kearne_build_options INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(kearne_build_options INTERFACE
            -fsanitize=address,undefined)
    endif()
endif()

if(BUILD_TESTING)
    add_subdirectory(testkit)
endif()
add_subdirectory(modules/base)
add_subdirectory(modules/document)
add_subdirectory(modules/sketch)
add_subdirectory(modules/adapters/sketch_occ)
add_subdirectory(modules/render)
add_subdirectory(modules/evaluation)
add_subdirectory(modules/adapters/sketch_ceres)
add_subdirectory(modules/sketch_runtime)

if(KEARNE_BUILD_API)
    add_subdirectory(api/schema)
    add_subdirectory(modules/adapters/sketch_wire)
    add_subdirectory(modules/engineering)
    add_subdirectory(modules/sketch_workflow)
endif()

if(BUILD_TESTING)
    find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
    add_subdirectory(sdk/python)
    add_test(
        NAME production-build-graph
        COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tools/verify/production_graph.py
                --source-root ${PROJECT_SOURCE_DIR}
                --build-root ${PROJECT_BINARY_DIR}
    )
    add_test(
        NAME documentation-links
        COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tools/verify/documentation.py
                --source-root ${PROJECT_SOURCE_DIR}
    )
endif()

if(KEARNE_BUILD_DESKTOP)
    add_subdirectory(modules/adapters/qt_process)
    add_subdirectory(modules/adapters/sketch_source_worker)
    add_subdirectory(apps/desktop)
endif()

# prototype/ is intentionally not part of the production build graph.
