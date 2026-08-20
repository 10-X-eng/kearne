add_library(kearne_sketch_workflow STATIC src/workflow.cpp)
add_library(Kearne::SketchWorkflow ALIAS kearne_sketch_workflow)
target_include_directories(kearne_sketch_workflow
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(kearne_sketch_workflow
    PUBLIC Kearne::Engineering Kearne::SketchRuntime
    PRIVATE Kearne::BuildOptions)

install(TARGETS kearne_sketch_workflow EXPORT KearneTargets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(BUILD_TESTING)
    add_executable(kearne_sketch_workflow_properties
        tests/workflow_properties.cpp)
    target_link_libraries(kearne_sketch_workflow_properties PRIVATE
        Kearne::SketchWorkflow Kearne::Testkit Kearne::BuildOptions)
    add_test(NAME sketch-workflow-properties
             COMMAND kearne_sketch_workflow_properties)
    set_tests_properties(sketch-workflow-properties PROPERTIES
        LABELS "sketch;workflow;property;contract"
        TIMEOUT 120)
endif()
