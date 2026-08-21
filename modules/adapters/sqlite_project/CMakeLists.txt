kearne_require_sqlite()

add_library(kearne_sqlite_project STATIC src/sqlite_project_store.cpp)
add_library(Kearne::SQLiteProject ALIAS kearne_sqlite_project)
target_include_directories(kearne_sqlite_project
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(kearne_sqlite_project
    PUBLIC Kearne::Persistence
    PRIVATE Kearne::SQLite Kearne::BuildOptions)

install(TARGETS kearne_sqlite_project EXPORT KearneTargets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(BUILD_TESTING)
    add_executable(kearne_sqlite_project_properties
        tests/sqlite_project_properties.cpp)
    target_link_libraries(kearne_sqlite_project_properties PRIVATE
        Kearne::SQLiteProject Kearne::SQLite Kearne::DocumentInternal Kearne::Testkit
        Kearne::BuildOptions)
    add_test(NAME sqlite-project-properties
        COMMAND kearne_sqlite_project_properties)
    set_tests_properties(sqlite-project-properties PROPERTIES TIMEOUT 120)
endif()
