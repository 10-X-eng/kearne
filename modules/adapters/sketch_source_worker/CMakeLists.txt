find_package(Qt6 6.8 REQUIRED COMPONENTS Core)

add_library(kearne_sketch_source_worker STATIC
    src/sketch_source_worker.cpp)
add_library(Kearne::SketchSourceWorker ALIAS kearne_sketch_source_worker)
target_include_directories(kearne_sketch_source_worker
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(kearne_sketch_source_worker
    PUBLIC Kearne::ApiWire Kearne::QtProcess Kearne::SketchWorkflow
    PRIVATE Kearne::SketchWire Kearne::BuildOptions)

install(TARGETS kearne_sketch_source_worker EXPORT KearneTargets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(BUILD_TESTING)
    add_executable(kearne_sketch_source_worker_contract_tests
        tests/sketch_source_worker_contract.cpp)
    target_link_libraries(kearne_sketch_source_worker_contract_tests PRIVATE
        Kearne::SketchSourceWorker Kearne::SketchWire Kearne::SketchCeres
        Kearne::Testkit Kearne::BuildOptions Qt6::Core)
    get_property(kearne_generated_python_root GLOBAL PROPERTY
        KEARNE_API_GENERATED_PYTHON_ROOT)
    target_compile_definitions(kearne_sketch_source_worker_contract_tests
        PRIVATE
        "KEARNE_TEST_PYTHON=\"$<TARGET_FILE:Python3::Interpreter>\""
        "KEARNE_TEST_SDK_ROOT=\"${PROJECT_SOURCE_DIR}/sdk/python/src\""
        "KEARNE_TEST_GENERATED_PYTHON_ROOT=\"${kearne_generated_python_root}\"")
    add_test(NAME sketch-source-worker-contracts
             COMMAND kearne_sketch_source_worker_contract_tests)
    set_tests_properties(sketch-source-worker-contracts PROPERTIES
        LABELS "worker;contract;source"
        TIMEOUT 60)
endif()
