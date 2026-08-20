find_package(Qt6 6.8 REQUIRED COMPONENTS Core)

add_library(kearne_qt_process STATIC
    src/framed_worker_process.cpp)
add_library(Kearne::QtProcess ALIAS kearne_qt_process)
target_include_directories(kearne_qt_process
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(kearne_qt_process
    PUBLIC Kearne::Base Qt6::Core
    PRIVATE Kearne::BuildOptions)

install(TARGETS kearne_qt_process EXPORT KearneTargets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(BUILD_TESTING)
    add_executable(kearne_qt_process_contract_tests
        tests/framed_worker_process_contract.cpp)
    target_link_libraries(kearne_qt_process_contract_tests PRIVATE
        Kearne::QtProcess Kearne::Testkit Kearne::BuildOptions Qt6::Core)
    target_compile_definitions(kearne_qt_process_contract_tests PRIVATE
        "KEARNE_TEST_PYTHON=\"$<TARGET_FILE:Python3::Interpreter>\""
        "KEARNE_TEST_WORKER_SCRIPT=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/framed_worker_fixture.py\"")
    add_test(NAME qt-process-framing-contracts
             COMMAND kearne_qt_process_contract_tests)
    set_tests_properties(qt-process-framing-contracts PROPERTIES
        LABELS "worker;contract;generated;fault"
        TIMEOUT 60)
endif()
