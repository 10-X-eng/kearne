add_library(kearne_sketch STATIC src/edit.cpp src/model.cpp src/tools.cpp)
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
        TIMEOUT 60
    )
endif()
