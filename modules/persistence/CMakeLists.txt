add_library(kearne_persistence INTERFACE)
add_library(Kearne::Persistence ALIAS kearne_persistence)
target_include_directories(kearne_persistence INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(kearne_persistence INTERFACE
    Kearne::Document Kearne::Engineering)

install(TARGETS kearne_persistence EXPORT KearneTargets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
