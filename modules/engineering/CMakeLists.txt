add_library(kearne_engineering STATIC
    src/request_identity.cpp
    src/revision.cpp
    src/service.cpp)
add_library(Kearne::Engineering ALIAS kearne_engineering)
target_include_directories(kearne_engineering
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/internal")
target_link_libraries(kearne_engineering
    PUBLIC Kearne::ApiWire Kearne::Document
    PRIVATE Kearne::DocumentInternal Kearne::BuildOptions)

install(TARGETS kearne_engineering EXPORT KearneTargets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(BUILD_TESTING)
    add_executable(kearne_engineering_model tests/service_model.cpp)
    target_link_libraries(kearne_engineering_model PRIVATE
        Kearne::Engineering Kearne::Testkit Kearne::BuildOptions)
    add_test(NAME engineering-model COMMAND kearne_engineering_model)
    add_executable(kearne_engineering_request_identity_properties
        tests/request_identity_properties.cpp)
    target_link_libraries(kearne_engineering_request_identity_properties PRIVATE
        Kearne::Engineering Kearne::Testkit Kearne::BuildOptions)
    add_test(NAME engineering-request-identity-properties
        COMMAND kearne_engineering_request_identity_properties)
    add_executable(kearne_engineering_revision_properties
        tests/revision_properties.cpp)
    target_link_libraries(kearne_engineering_revision_properties PRIVATE
        Kearne::Engineering Kearne::Testkit Kearne::BuildOptions)
    add_test(NAME engineering-revision-properties
        COMMAND kearne_engineering_revision_properties)
    if(KEARNE_ENABLE_SANITIZERS)
        set_tests_properties(
            engineering-model engineering-request-identity-properties
            engineering-revision-properties
            PROPERTIES TIMEOUT 120)
    else()
        set_tests_properties(
            engineering-model engineering-request-identity-properties
            engineering-revision-properties
            PROPERTIES TIMEOUT 60)
    endif()
endif()
