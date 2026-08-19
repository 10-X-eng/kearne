cmake_minimum_required(VERSION 3.28)

project(Kearne VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

option(KEARNE_BUILD_DESKTOP "Build the Qt desktop application" ON)

if(BUILD_TESTING)
    find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
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
    add_subdirectory(apps/desktop)
endif()

# prototype/ is intentionally not part of the production build graph.
