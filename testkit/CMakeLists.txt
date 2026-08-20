add_library(kearne_testkit INTERFACE)
add_library(Kearne::Testkit ALIAS kearne_testkit)
target_include_directories(kearne_testkit INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(kearne_testkit INTERFACE Kearne::BuildOptions)

add_executable(kearne_testkit_distribution_properties
    tests/distribution_properties.cpp)
target_link_libraries(kearne_testkit_distribution_properties
    PRIVATE Kearne::Testkit)
add_test(NAME testkit-distribution-properties
    COMMAND kearne_testkit_distribution_properties)
set_tests_properties(testkit-distribution-properties PROPERTIES
    LABELS "property;performance"
    TIMEOUT 10)

add_executable(kearne_testkit_property_properties
    tests/property_properties.cpp)
target_link_libraries(kearne_testkit_property_properties
    PRIVATE Kearne::Testkit)
add_test(NAME testkit-property-properties
    COMMAND kearne_testkit_property_properties)
set_tests_properties(testkit-property-properties PROPERTIES
    LABELS "property;contract"
    TIMEOUT 10)
