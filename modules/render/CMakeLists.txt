add_library(kearne_render STATIC src/sketch_scene.cpp)
add_library(Kearne::Render ALIAS kearne_render)
target_include_directories(kearne_render
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(kearne_render
    PUBLIC Kearne::Base Kearne::Sketch
    PRIVATE Kearne::BuildOptions)

install(TARGETS kearne_render EXPORT KearneTargets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(BUILD_TESTING)
    add_executable(kearne_render_properties tests/scene_properties.cpp)
    target_link_libraries(kearne_render_properties PRIVATE
        Kearne::Render Kearne::Testkit Kearne::BuildOptions)
    add_test(NAME render-properties COMMAND kearne_render_properties)
    set_tests_properties(render-properties PROPERTIES TIMEOUT 60)

    add_executable(kearne_render_pick_benchmark EXCLUDE_FROM_ALL
        tests/pick_benchmark.cpp)
    target_link_libraries(kearne_render_pick_benchmark PRIVATE
        Kearne::Render Kearne::Testkit Kearne::BuildOptions)
endif()
