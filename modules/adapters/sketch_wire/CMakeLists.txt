add_library(kearne_sketch_wire STATIC src/sketch_wire.cpp)
add_library(Kearne::SketchWire ALIAS kearne_sketch_wire)
target_include_directories(kearne_sketch_wire
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(kearne_sketch_wire
    PUBLIC Kearne::ApiWire Kearne::Sketch
    PRIVATE Kearne::BuildOptions)

install(TARGETS kearne_sketch_wire EXPORT KearneTargets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(BUILD_TESTING)
    find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
    add_executable(kearne_sketch_wire_properties
        tests/sketch_wire_properties.cpp)
    target_link_libraries(kearne_sketch_wire_properties PRIVATE
        Kearne::SketchWire Kearne::Testkit Kearne::BuildOptions)
    add_test(NAME sketch-wire-properties COMMAND kearne_sketch_wire_properties)
    set_tests_properties(sketch-wire-properties PROPERTIES
        LABELS "property;contract"
        TIMEOUT 180)

    add_library(kearne_sketch_wire_fuzz OBJECT tests/sketch_wire_fuzz.cpp)
    target_link_libraries(kearne_sketch_wire_fuzz PRIVATE
        Kearne::SketchWire Kearne::BuildOptions)
    add_executable(kearne_sketch_wire_fuzz_smoke
        tests/sketch_wire_fuzz_smoke.cpp
        $<TARGET_OBJECTS:kearne_sketch_wire_fuzz>)
    target_link_libraries(kearne_sketch_wire_fuzz_smoke PRIVATE
        Kearne::SketchWire Kearne::Testkit Kearne::BuildOptions)
    add_test(NAME sketch-wire-fuzz-smoke COMMAND kearne_sketch_wire_fuzz_smoke)
    set_tests_properties(sketch-wire-fuzz-smoke PROPERTIES
        LABELS "fuzz;contract"
        TIMEOUT 60)

    add_executable(kearne_sketch_wire_bridge tests/sketch_wire_bridge.cpp)
    target_link_libraries(kearne_sketch_wire_bridge PRIVATE
        Kearne::SketchWire Kearne::BuildOptions)
    get_property(generated_python_root GLOBAL PROPERTY
        KEARNE_API_GENERATED_PYTHON_ROOT)
    add_test(
        NAME sketch-wire-python-properties
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_properties.py"
                --bridge "$<TARGET_FILE:kearne_sketch_wire_bridge>"
                --python-root "${generated_python_root}"
                --sdk-root "${PROJECT_SOURCE_DIR}/sdk/python/src")
    set_tests_properties(sketch-wire-python-properties PROPERTIES
        LABELS "property;contract"
        TIMEOUT 60)

    add_executable(kearne_sketch_wire_benchmark EXCLUDE_FROM_ALL
        benchmarks/sketch_wire_benchmark.cpp)
    target_link_libraries(kearne_sketch_wire_benchmark PRIVATE
        Kearne::SketchWire Kearne::Testkit Kearne::BuildOptions)
endif()
