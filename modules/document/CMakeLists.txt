kearne_require_document_dependencies()

add_library(kearne_document STATIC
    src/canonical.cpp
    src/checkpoint.cpp
    src/content_store.cpp
    src/document.cpp)
add_library(Kearne::Document ALIAS kearne_document)
target_include_directories(kearne_document
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/internal")
target_link_libraries(kearne_document
    PUBLIC Kearne::Base
    PRIVATE Kearne::Blake3 Kearne::Immer Kearne::BuildOptions)

add_library(kearne_document_internal INTERFACE)
add_library(Kearne::DocumentInternal ALIAS kearne_document_internal)
target_include_directories(kearne_document_internal INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/internal")
target_link_libraries(kearne_document_internal INTERFACE Kearne::Document)

install(TARGETS kearne_document EXPORT KearneTargets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(BUILD_TESTING)
    add_executable(kearne_document_properties tests/document_properties.cpp)
    target_link_libraries(kearne_document_properties PRIVATE
        Kearne::Document Kearne::DocumentInternal Kearne::Testkit
        Kearne::BuildOptions)
    add_test(NAME document-properties COMMAND kearne_document_properties)

    add_executable(kearne_document_local_edit_benchmark EXCLUDE_FROM_ALL
        benchmarks/local_edit_scaling.cpp)
    target_link_libraries(kearne_document_local_edit_benchmark PRIVATE
        Kearne::Document Kearne::DocumentInternal Kearne::Testkit
        Kearne::BuildOptions)
    set_tests_properties(document-properties PROPERTIES TIMEOUT 120)
    if(KEARNE_ENABLE_SANITIZERS)
        set_tests_properties(document-properties PROPERTIES
            ENVIRONMENT "KEARNE_TEST_ITERATIONS=1000")
    endif()
endif()
