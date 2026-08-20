add_library(kearne_evaluation STATIC src/scheduler.cpp)
add_library(Kearne::Evaluation ALIAS kearne_evaluation)
target_include_directories(kearne_evaluation
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(kearne_evaluation
    PUBLIC Kearne::Base
    PRIVATE Kearne::BuildOptions)

install(TARGETS kearne_evaluation EXPORT KearneTargets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(BUILD_TESTING)
    add_executable(kearne_evaluation_scheduler_properties
        tests/scheduler_properties.cpp)
    target_link_libraries(kearne_evaluation_scheduler_properties PRIVATE
        Kearne::Evaluation Kearne::Testkit Kearne::BuildOptions)
    add_test(NAME evaluation-scheduler-properties
        COMMAND kearne_evaluation_scheduler_properties)
    set_tests_properties(evaluation-scheduler-properties PROPERTIES
        LABELS "property;model;contract"
        TIMEOUT 300)

    add_executable(kearne_evaluation_scheduler_benchmark EXCLUDE_FROM_ALL
        benchmarks/scheduler_benchmark.cpp)
    target_link_libraries(kearne_evaluation_scheduler_benchmark PRIVATE
        Kearne::Evaluation Kearne::Testkit Kearne::BuildOptions)
endif()
