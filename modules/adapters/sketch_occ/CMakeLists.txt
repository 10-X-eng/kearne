find_package(OpenCASCADE REQUIRED COMPONENTS
    FoundationClasses
    ModelingData
    ModelingAlgorithms
)

add_library(kearne_sketch_occ STATIC src/occ_bspline.cpp)
add_library(Kearne::SketchOcc ALIAS kearne_sketch_occ)
target_include_directories(kearne_sketch_occ
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_include_directories(kearne_sketch_occ SYSTEM PRIVATE
    "${OpenCASCADE_INCLUDE_DIR}")
target_link_libraries(kearne_sketch_occ
    PUBLIC Kearne::Sketch
    PRIVATE TKGeomAlgo TKGeomBase TKG3d TKG2d TKMath TKernel
            Kearne::BuildOptions
)

if(BUILD_TESTING)
    add_executable(kearne_sketch_occ_properties tests/occ_bspline_properties.cpp)
    target_link_libraries(kearne_sketch_occ_properties PRIVATE
        Kearne::SketchOcc Kearne::Testkit Kearne::BuildOptions)
    add_test(NAME sketch-occ-properties COMMAND kearne_sketch_occ_properties)
    set_tests_properties(sketch-occ-properties PROPERTIES
        LABELS "property;contract"
        TIMEOUT 180
    )
endif()
