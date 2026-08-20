add_library(kearne_base INTERFACE)
add_library(Kearne::Base ALIAS kearne_base)
target_include_directories(kearne_base INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(kearne_base INTERFACE Kearne::BuildOptions)

install(TARGETS kearne_base EXPORT KearneTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING)
    add_executable(kearne_base_properties tests/value_properties.cpp)
    target_link_libraries(kearne_base_properties PRIVATE
        Kearne::Base Kearne::Testkit)
    add_test(NAME base-properties COMMAND kearne_base_properties)
    set_tests_properties(base-properties PROPERTIES TIMEOUT 30)
endif()
