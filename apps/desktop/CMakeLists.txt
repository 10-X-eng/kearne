find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(kearne
    src/main.cpp
    src/ui_session.cpp
    src/ui_session.hpp
)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(kearne
    URI Kearne.UI
    VERSION 1.0
    NO_IMPORT_SCAN
    QML_FILES
        qml/Theme.qml
        qml/Main.qml
        qml/pages/EditorPage.qml
        qml/pages/ProjectHubPage.qml
        qml/pages/SettingsPage.qml
        qml/pages/RecoveryPage.qml
        qml/pages/OperationInspectorPage.qml
        qml/components/KButton.qml
        qml/components/KIcon.qml
        qml/components/KChoice.qml
        qml/components/KToggle.qml
        qml/components/KTextField.qml
        qml/components/KPanel.qml
        qml/components/PageHeading.qml
        qml/components/StateBadge.qml
        qml/components/SettingRow.qml
        qml/components/SurfaceState.qml
        qml/components/ProjectBar.qml
        qml/components/WorkspaceBar.qml
        qml/components/CommandStrip.qml
        qml/components/StructurePanel.qml
        qml/components/EngineeringGrid.qml
        qml/components/DesignViewport.qml
        qml/components/InspectorPanel.qml
        qml/components/ActivityPanel.qml
        qml/components/StatusBar.qml
        qml/components/CommandPalette.qml
)

target_compile_definitions(kearne PRIVATE
    KEARNE_VERSION="${PROJECT_VERSION}"
    KEARNE_BUILD_TYPE="$<CONFIG>"
)

if(MSVC)
    target_compile_options(kearne PRIVATE /W4 /permissive-)
else()
    target_compile_options(kearne PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

target_link_libraries(kearne PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

install(TARGETS kearne
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

if(BUILD_TESTING)
    add_test(
        NAME desktop-observation-smoke
        COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tools/observe/desktop_matrix.py
                --executable $<TARGET_FILE:kearne>
                --mode smoke
    )
    set_tests_properties(desktop-observation-smoke PROPERTIES TIMEOUT 180)

    set(KEARNE_TEST_INSTALL_PREFIX "${PROJECT_BINARY_DIR}/test-install")
    add_test(
        NAME production-install
        COMMAND ${CMAKE_COMMAND} --install ${PROJECT_BINARY_DIR}
                --config $<CONFIG>
                --prefix ${KEARNE_TEST_INSTALL_PREFIX}
    )
    set_tests_properties(production-install PROPERTIES
        FIXTURES_SETUP installed-desktop
        TIMEOUT 60
    )

    add_test(
        NAME installed-desktop-observation
        COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tools/observe/desktop_matrix.py
                --executable ${KEARNE_TEST_INSTALL_PREFIX}/bin/kearne${CMAKE_EXECUTABLE_SUFFIX}
                --mode smoke
    )
    set_tests_properties(installed-desktop-observation PROPERTIES
        FIXTURES_REQUIRED installed-desktop
        TIMEOUT 180
    )
endif()
