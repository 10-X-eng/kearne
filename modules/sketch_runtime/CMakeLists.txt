add_library(kearne_sketch_runtime STATIC src/runtime.cpp)
add_library(Kearne::SketchRuntime ALIAS kearne_sketch_runtime)
target_include_directories(kearne_sketch_runtime
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(kearne_sketch_runtime
    PUBLIC Kearne::Render Kearne::Sketch
    PRIVATE Kearne::BuildOptions)

install(TARGETS kearne_sketch_runtime EXPORT KearneTargets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(BUILD_TESTING)
    add_executable(kearne_sketch_runtime_contract
        tests/runtime_contract.cpp)
    target_link_libraries(kearne_sketch_runtime_contract PRIVATE
        Kearne::SketchRuntime Kearne::Testkit Kearne::BuildOptions)
    add_test(NAME sketch-runtime-contract
        COMMAND kearne_sketch_runtime_contract)
    set_tests_properties(sketch-runtime-contract PROPERTIES
        LABELS "property;contract"
        TIMEOUT 60)

    add_executable(kearne_sketch_runtime_benchmark EXCLUDE_FROM_ALL
        benchmarks/orchestration_benchmark.cpp)
    target_link_libraries(kearne_sketch_runtime_benchmark PRIVATE
        Kearne::SketchRuntime Kearne::Testkit Kearne::BuildOptions)
endif()
