add_library(kearne_sketch STATIC
    src/edit.cpp
    src/modify.cpp
    src/model.cpp
    src/nurbs.cpp
    src/tools.cpp
    src/transform.cpp
)
add_library(Kearne::Sketch ALIAS kearne_sketch)
target_include_directories(kearne_sketch
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(kearne_sketch
    PUBLIC Kearne::Base
    PRIVATE Kearne::BuildOptions)

install(TARGETS kearne_sketch EXPORT KearneTargets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(BUILD_TESTING)
    add_executable(kearne_sketch_model_properties tests/model_properties.cpp)
    target_link_libraries(kearne_sketch_model_properties PRIVATE
        Kearne::Sketch
        Kearne::Testkit
        Kearne::BuildOptions
    )
    add_test(NAME sketch-model-properties
        COMMAND kearne_sketch_model_properties)
    set_tests_properties(sketch-model-properties PROPERTIES
        LABELS "property;contract"
        TIMEOUT 180
    )

    add_executable(kearne_sketch_nurbs_properties tests/nurbs_properties.cpp)
    target_link_libraries(kearne_sketch_nurbs_properties PRIVATE
        Kearne::Sketch
        Kearne::Testkit
        Kearne::BuildOptions
    )
    add_test(NAME sketch-nurbs-properties
        COMMAND kearne_sketch_nurbs_properties)
    set_tests_properties(sketch-nurbs-properties PROPERTIES
        LABELS "property;generated;render;sketch"
        TIMEOUT 180
    )

    add_executable(kearne_sketch_transform_properties
        tests/transform_properties.cpp)
    target_link_libraries(kearne_sketch_transform_properties PRIVATE
        Kearne::Sketch
        Kearne::Testkit
        Kearne::BuildOptions
    )
    add_test(NAME sketch-transform-properties
        COMMAND kearne_sketch_transform_properties)
    set_tests_properties(sketch-transform-properties PROPERTIES
        LABELS "property;generated;sketch"
        TIMEOUT 180
    )

    add_executable(kearne_sketch_modify_properties tests/modify_properties.cpp)
    target_link_libraries(kearne_sketch_modify_properties PRIVATE
        Kearne::Sketch
        Kearne::Testkit
        Kearne::BuildOptions
    )
    add_test(NAME sketch-modify-properties
        COMMAND kearne_sketch_modify_properties)
    set_tests_properties(sketch-modify-properties PROPERTIES
        LABELS "property;generated;sketch"
        TIMEOUT 180
    )
endif()
