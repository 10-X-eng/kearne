kearne_require_sketch_solver()

add_library(kearne_sketch_ceres STATIC src/ceres_sketch_solver.cpp)
add_library(Kearne::SketchCeres ALIAS kearne_sketch_ceres)
target_include_directories(kearne_sketch_ceres PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(kearne_sketch_ceres
    PUBLIC Kearne::Sketch
    PRIVATE Ceres::ceres Eigen3::Eigen Kearne::BuildOptions)
if(KEARNE_ENABLE_SANITIZERS)
    # Ceres does not inherit Kearne's target-scoped sanitizer flags. Keep
    # Eigen's inline allocator ABI identical across the instrumented adapter
    # and Ceres; ASan's allocator satisfies Eigen's 16-byte requirement.
    target_compile_definitions(kearne_sketch_ceres PRIVATE
        EIGEN_MALLOC_ALREADY_ALIGNED=1)
endif()

if(BUILD_TESTING)
    add_executable(kearne_sketch_solver_properties
        tests/solver_properties.cpp)
    target_link_libraries(kearne_sketch_solver_properties PRIVATE
        Kearne::SketchCeres Kearne::Testkit Kearne::BuildOptions)
    add_test(NAME sketch-solver-properties
        COMMAND kearne_sketch_solver_properties)
    add_executable(kearne_sketch_solver_benchmark EXCLUDE_FROM_ALL
        tests/solver_benchmark.cpp)
    target_link_libraries(kearne_sketch_solver_benchmark PRIVATE
        Kearne::SketchCeres Kearne::Testkit Kearne::BuildOptions)
    if(KEARNE_ENABLE_SANITIZERS)
        set_tests_properties(sketch-solver-properties PROPERTIES TIMEOUT 180)
    else()
        set_tests_properties(sketch-solver-properties PROPERTIES TIMEOUT 60)
    endif()
endif()
