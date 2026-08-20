find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Gui GuiPrivate Qml Quick QuickControls2 QuickDialogs2)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(PkgConfig QUIET)
if(UNIX AND NOT APPLE AND PkgConfig_FOUND)
    pkg_check_modules(SPNAV QUIET IMPORTED_TARGET spnav)
endif()

qt_standard_project_setup(REQUIRES 6.8)

get_target_property(KEARNE_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
execute_process(
    COMMAND "${KEARNE_QMAKE_EXECUTABLE}" -query QT_HOST_BINS
    OUTPUT_VARIABLE KEARNE_QT_HOST_BIN_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
find_program(KEARNE_QT6_QSB_EXECUTABLE NAMES qsb
    PATHS "${KEARNE_QT_HOST_BIN_DIR}" NO_DEFAULT_PATH REQUIRED NO_CACHE)

set(KEARNE_SKETCH_SHADER_OUTPUT_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/sketch_scene_shaders")
set(KEARNE_SKETCH_SHADER_OUTPUTS)
foreach(shader IN ITEMS sketch_scene.vert sketch_scene.frag)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}")
    set(output "${KEARNE_SKETCH_SHADER_OUTPUT_DIR}/${shader}.qsb")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${KEARNE_SKETCH_SHADER_OUTPUT_DIR}"
        COMMAND "${KEARNE_QT6_QSB_EXECUTABLE}" --qt6 -O -o "${output}"
                "${source}"
        DEPENDS "${source}"
        VERBATIM
    )
    set_source_files_properties("${output}" PROPERTIES
        GENERATED TRUE
        QT_RESOURCE_ALIAS "${shader}.qsb"
    )
    list(APPEND KEARNE_SKETCH_SHADER_OUTPUTS "${output}")
endforeach()

add_library(kearne_sketch_scene_quick STATIC
    src/sketch_frame_renderer.cpp
    src/sketch_frame_renderer.hpp
    src/sketch_marker_projection.cpp
    src/sketch_marker_projection.hpp
    src/sketch_overlay_projection.cpp
    src/sketch_overlay_projection.hpp
    src/sketch_prepared_products.cpp
    src/sketch_prepared_products.hpp
    src/sketch_projection_support.hpp
    src/sketch_scene_item.cpp
    src/sketch_scene_item.hpp
    src/sketch_scene_projection.cpp
    src/sketch_scene_projection.hpp
    src/sketch_scene_products.cpp
    src/sketch_scene_products.hpp
    src/sketch_scene_publication.cpp
    src/sketch_scene_publication.hpp
    src/sketch_stroke_mesh.cpp
    src/sketch_stroke_mesh.hpp
    src/sketch_stroke_mesh_build.hpp
    src/sketch_stroke_pattern.hpp
)
add_library(Kearne::SketchSceneQuick ALIAS kearne_sketch_scene_quick)
target_include_directories(kearne_sketch_scene_quick PUBLIC src)
target_link_libraries(kearne_sketch_scene_quick
    PUBLIC Kearne::Render Qt6::Core Qt6::Gui Qt6::Quick
    PRIVATE Kearne::BuildOptions Qt6::GuiPrivate
)
qt_add_resources(kearne_sketch_scene_quick kearne_sketch_scene_shaders
    PREFIX "/kearne/shaders"
    FILES ${KEARNE_SKETCH_SHADER_OUTPUTS}
)

add_library(kearne_local_sketch_session STATIC
    src/local_sketch_session.cpp
    src/local_sketch_session.hpp
)
add_library(Kearne::LocalSketchSession ALIAS kearne_local_sketch_session)
target_include_directories(kearne_local_sketch_session PUBLIC src)
target_link_libraries(kearne_local_sketch_session
    PUBLIC Kearne::Render Qt6::Core
    PRIVATE
        Kearne::BuildOptions
        Kearne::SketchCeres
        Kearne::SketchSourceWorker
        Kearne::SketchWorkflow
)

qt_add_executable(kearne
    src/application_context.cpp
    src/application_context.hpp
    src/development_frontend_port.cpp
    src/development_frontend_port.hpp
    src/engineering_grid_item.cpp
    src/engineering_grid_item.hpp
    src/frontend_contract.hpp
    src/local_json_store.cpp
    src/local_json_store.hpp
    src/main.cpp
    src/navigation_device.cpp
    src/navigation_device.hpp
    src/navigation_input.cpp
    src/navigation_input.hpp
    src/observation_controller.cpp
    src/observation_controller.hpp
    src/sketch_camera_controller.cpp
    src/sketch_camera_controller.hpp
    src/theme_manager.cpp
    src/theme_manager.hpp
    src/ui_session.cpp
    src/ui_session.hpp
    src/user_preferences.cpp
    src/user_preferences.hpp
    src/viewport_camera.cpp
    src/viewport_camera.hpp
    src/workspace_state.cpp
    src/workspace_state.hpp
)

qt_add_resources(kearne kearne_themes
    PREFIX "/kearne/themes"
    BASE themes
    FILES
        themes/light.yml
        themes/dark.yml
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
        qml/components/KSeparator.qml
        qml/components/KSplitHandle.qml
        qml/components/PageHeading.qml
        qml/components/StateBadge.qml
        qml/components/SettingRow.qml
        qml/components/SurfaceState.qml
        qml/components/ProjectBar.qml
        qml/components/WorkspaceBar.qml
        qml/components/CommandStrip.qml
        qml/components/StructurePanel.qml
        qml/components/EngineeringGrid.qml
        qml/components/BoxEdges3D.qml
        qml/components/NavigationCalibrationScene.qml
        qml/components/SketchCalibrationScene.qml
        qml/components/OrientationCube.qml
        qml/components/DesignViewport.qml
        qml/components/InspectorPanel.qml
        qml/components/FunctionContractPanel.qml
        qml/components/FunctionPortRow.qml
        qml/components/ThemeImportDialog.qml
        qml/components/SourceDiffDialog.qml
        qml/components/ParameterManagerDialog.qml
        qml/components/ActivityPanel.qml
        qml/components/StatusBar.qml
        qml/components/CommandPalette.qml
)

target_compile_definitions(kearne PRIVATE
    KEARNE_VERSION="${PROJECT_VERSION}"
    KEARNE_BUILD_TYPE="$<CONFIG>"
)

target_include_directories(kearne PRIVATE src)

target_link_libraries(kearne PRIVATE
    Kearne::BuildOptions
    Kearne::LocalSketchSession
    Kearne::SketchSceneQuick
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2
    yaml-cpp::yaml-cpp
)

if(TARGET PkgConfig::SPNAV)
    target_compile_definitions(kearne PRIVATE KEARNE_HAS_SPNAV=1)
    target_link_libraries(kearne PRIVATE PkgConfig::SPNAV)
endif()

install(TARGETS kearne
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
install(PROGRAMS ${PROJECT_SOURCE_DIR}/tools/observe/desktop_matrix.py
        DESTINATION ${CMAKE_INSTALL_DATADIR}/kearne/observe)
install(FILES ${PROJECT_SOURCE_DIR}/tools/observe/observation.schema.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/kearne/observe)

if(BUILD_TESTING)
    add_executable(kearne_local_sketch_session_contract_tests
        tests/local_sketch_session_contract.cpp
    )
    target_link_libraries(kearne_local_sketch_session_contract_tests PRIVATE
        Kearne::LocalSketchSession Kearne::Testkit Kearne::BuildOptions
        Qt6::Core)
    get_property(kearne_desktop_generated_python_root GLOBAL PROPERTY
        KEARNE_API_GENERATED_PYTHON_ROOT)
    target_compile_definitions(kearne_local_sketch_session_contract_tests
        PRIVATE
        "KEARNE_TEST_PYTHON=\"$<TARGET_FILE:Python3::Interpreter>\""
        "KEARNE_TEST_SDK_ROOT=\"${PROJECT_SOURCE_DIR}/sdk/python/src\""
        "KEARNE_TEST_GENERATED_PYTHON_ROOT=\"${kearne_desktop_generated_python_root}\"")
    add_test(NAME local-sketch-session-contracts
             COMMAND kearne_local_sketch_session_contract_tests)
    set_tests_properties(local-sketch-session-contracts PROPERTIES
        LABELS "desktop;sketch;workflow;contract;concurrency"
        TIMEOUT 60)

    add_executable(kearne_navigation_input_contract_tests
        tests/navigation_input_contract.cpp
        src/navigation_input.cpp
        src/navigation_input.hpp
    )
    target_include_directories(kearne_navigation_input_contract_tests
        PRIVATE src)
    target_link_libraries(kearne_navigation_input_contract_tests PRIVATE
        Kearne::Testkit
        Kearne::BuildOptions
        Qt6::Core
    )
    add_test(NAME navigation-input-properties
             COMMAND kearne_navigation_input_contract_tests)
    set_tests_properties(navigation-input-properties PROPERTIES
        LABELS "navigation;contract;generated"
        TIMEOUT 30)

    if(TARGET PkgConfig::SPNAV)
        add_executable(kearne_navigation_device_contract_tests
            tests/navigation_device_contract.cpp
            src/navigation_device.cpp
            src/navigation_device.hpp
            src/navigation_input.cpp
            src/navigation_input.hpp
            src/viewport_camera.cpp
            src/viewport_camera.hpp
        )
        target_compile_definitions(kearne_navigation_device_contract_tests
            PRIVATE KEARNE_HAS_SPNAV=1)
        target_include_directories(kearne_navigation_device_contract_tests
            PRIVATE src ${SPNAV_INCLUDE_DIRS})
        target_link_libraries(kearne_navigation_device_contract_tests PRIVATE
            Kearne::Testkit
            Kearne::BuildOptions
            Qt6::Core
            Qt6::Qml
        )
        add_test(NAME navigation-device-drain-contracts
                 COMMAND kearne_navigation_device_contract_tests)
        set_tests_properties(navigation-device-drain-contracts PROPERTIES
            LABELS "navigation;contract;generated"
            TIMEOUT 60)
    endif()

    add_executable(kearne_sketch_scene_item_contract_tests
        tests/sketch_scene_item_contract.cpp
        tests/sketch_scene_fixture.hpp
    )
    target_link_libraries(kearne_sketch_scene_item_contract_tests PRIVATE
        Kearne::SketchSceneQuick
        Kearne::Testkit
        Kearne::BuildOptions
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
    )
    add_test(NAME sketch-scene-item-contracts
             COMMAND kearne_sketch_scene_item_contract_tests)
    set_tests_properties(sketch-scene-item-contracts PROPERTIES TIMEOUT 120)

    add_executable(kearne_sketch_scene_publication_contract_tests
        tests/sketch_scene_publication_contract.cpp
        tests/sketch_scene_fixture.hpp
    )
    target_link_libraries(kearne_sketch_scene_publication_contract_tests PRIVATE
        Kearne::SketchSceneQuick
        Kearne::Testkit
        Kearne::BuildOptions
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
    )
    add_test(NAME sketch-scene-publication-contracts
             COMMAND kearne_sketch_scene_publication_contract_tests)
    set_tests_properties(sketch-scene-publication-contracts PROPERTIES
        LABELS "render;contract;generated;concurrency"
        TIMEOUT 180)

    function(kearne_add_sketch_projection_contract target test_name source)
        add_executable(${target} ${source} tests/sketch_scene_fixture.hpp)
        target_link_libraries(${target} PRIVATE
            Kearne::SketchSceneQuick
            Kearne::Testkit
            Kearne::BuildOptions
            Qt6::Core
            Qt6::Gui
        )
        add_test(NAME ${test_name} COMMAND ${target})
        set_tests_properties(${test_name} PROPERTIES
            LABELS "render;contract;generated"
            TIMEOUT 180)
    endfunction()

    kearne_add_sketch_projection_contract(
        kearne_sketch_overlay_projection_contract_tests
        sketch-overlay-projection-contracts
        tests/sketch_overlay_projection_contract.cpp)
    kearne_add_sketch_projection_contract(
        kearne_sketch_marker_projection_contract_tests
        sketch-marker-projection-contracts
        tests/sketch_marker_projection_contract.cpp)

    add_executable(kearne_sketch_scene_native_render_tests
        tests/sketch_scene_native_render.cpp
        tests/sketch_scene_fixture.hpp
    )
    target_link_libraries(kearne_sketch_scene_native_render_tests PRIVATE
        Kearne::SketchSceneQuick
        Kearne::Testkit
        Kearne::BuildOptions
        Qt6::Core
        Qt6::GuiPrivate
        Qt6::Quick
    )

    add_executable(kearne_engineering_grid_contract_tests
        tests/engineering_grid_item_contract.cpp
        src/engineering_grid_item.cpp
        src/engineering_grid_item.hpp
    )
    target_include_directories(kearne_engineering_grid_contract_tests
        PRIVATE src)
    target_link_libraries(kearne_engineering_grid_contract_tests PRIVATE
        Kearne::Testkit
        Kearne::BuildOptions
        Qt6::Gui
        Qt6::Quick
    )
    add_test(NAME engineering-grid-contracts
             COMMAND kearne_engineering_grid_contract_tests)
    set_tests_properties(engineering-grid-contracts PROPERTIES
        LABELS "render;contract;generated"
        TIMEOUT 60)

    add_executable(kearne_engineering_grid_native_render_tests
        tests/engineering_grid_native_render.cpp
        src/engineering_grid_item.cpp
        src/engineering_grid_item.hpp
    )
    target_include_directories(kearne_engineering_grid_native_render_tests
        PRIVATE src)
    target_link_libraries(kearne_engineering_grid_native_render_tests PRIVATE
        Kearne::BuildOptions
        Qt6::Core
        Qt6::GuiPrivate
        Qt6::Quick
    )

    set(KEARNE_NATIVE_RENDER_BACKENDS opengl)
    if(WIN32)
        list(APPEND KEARNE_NATIVE_RENDER_BACKENDS d3d11 d3d12)
    elseif(APPLE)
        list(APPEND KEARNE_NATIVE_RENDER_BACKENDS metal)
    elseif(UNIX)
        list(APPEND KEARNE_NATIVE_RENDER_BACKENDS vulkan)
    endif()
    function(kearne_add_native_render_tests target prefix)
        foreach(backend IN LISTS KEARNE_NATIVE_RENDER_BACKENDS)
            set(environment
                QT_QPA_PLATFORM=offscreen
                QT_QUICK_BACKEND=rhi
                QT_SCALE_FACTOR=1
            )
            if(KEARNE_ENABLE_SANITIZERS AND UNIX AND NOT APPLE AND
               backend STREQUAL "opengl")
                list(APPEND environment
                    "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tests/nvidia-opengl-lsan.supp")
            endif()
            add_test(NAME ${prefix}-${backend}
                COMMAND ${target} --backend ${backend})
            set_tests_properties(${prefix}-${backend} PROPERTIES
                ENVIRONMENT "${environment}"
                LABELS "render;native;contract"
                SKIP_RETURN_CODE 77
                TIMEOUT 60
            )
        endforeach()
    endfunction()

    kearne_add_native_render_tests(
        kearne_sketch_scene_native_render_tests sketch-scene-native-render)
    kearne_add_native_render_tests(
        kearne_engineering_grid_native_render_tests engineering-grid-native-render)

    add_executable(kearne_sketch_camera_contract_tests
        tests/sketch_camera_controller_contract.cpp
        src/sketch_camera_controller.cpp
        src/sketch_camera_controller.hpp
    )
    target_include_directories(kearne_sketch_camera_contract_tests PRIVATE src)
    target_link_libraries(kearne_sketch_camera_contract_tests PRIVATE
        Kearne::SketchSceneQuick
        Kearne::Testkit
        Kearne::BuildOptions
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
    )
    add_test(NAME sketch-camera-properties
             COMMAND kearne_sketch_camera_contract_tests)
    set_tests_properties(sketch-camera-properties PROPERTIES
        LABELS "navigation;property;generated"
        TIMEOUT 60)

    add_executable(kearne_sketch_scene_item_benchmark EXCLUDE_FROM_ALL
        tests/sketch_scene_item_benchmark.cpp
        tests/sketch_scene_fixture.hpp
    )
    target_link_libraries(kearne_sketch_scene_item_benchmark PRIVATE
        Kearne::SketchSceneQuick
        Kearne::Testkit
        Kearne::BuildOptions
    )

    qt_add_executable(kearne_ui_contract_tests
        tests/theme_preferences_contract.cpp
        src/theme_manager.cpp
        src/theme_manager.hpp
        src/local_json_store.cpp
        src/local_json_store.hpp
        src/user_preferences.cpp
        src/user_preferences.hpp
        src/navigation_input.cpp
        src/navigation_input.hpp
        src/viewport_camera.cpp
        src/viewport_camera.hpp
        src/workspace_state.cpp
        src/workspace_state.hpp
    )
    qt_add_resources(kearne_ui_contract_tests kearne_contract_test_themes
        PREFIX "/kearne/themes"
        BASE themes
        FILES
            themes/light.yml
            themes/dark.yml
    )
    target_include_directories(kearne_ui_contract_tests PRIVATE src)
    target_link_libraries(kearne_ui_contract_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        yaml-cpp::yaml-cpp
    )
    target_link_libraries(kearne_ui_contract_tests PRIVATE Kearne::BuildOptions)
    add_test(NAME theme-preference-contracts
             COMMAND kearne_ui_contract_tests)
    set_tests_properties(theme-preference-contracts PROPERTIES TIMEOUT 30)

    qt_add_executable(kearne_frontend_port_contract_tests
        tests/frontend_port_contract.cpp
        src/development_frontend_port.cpp
        src/development_frontend_port.hpp
        src/frontend_contract.hpp
    )
    target_include_directories(kearne_frontend_port_contract_tests PRIVATE src)
    target_link_libraries(kearne_frontend_port_contract_tests PRIVATE Qt6::Core)
    target_link_libraries(kearne_frontend_port_contract_tests PRIVATE
        Kearne::BuildOptions)
    add_test(NAME frontend-port-contracts
             COMMAND kearne_frontend_port_contract_tests)
    set_tests_properties(frontend-port-contracts PROPERTIES TIMEOUT 30)

    add_test(
        NAME qml-static-analysis
        COMMAND $<TARGET_FILE:Qt6::qmllint>
                --max-warnings 0
                --unused-imports warning
                "@${CMAKE_CURRENT_BINARY_DIR}/.rcc/qmllint/kearne.rsp"
    )
    set_tests_properties(qml-static-analysis PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        TIMEOUT 60
    )

    add_test(
        NAME desktop-observation-smoke
        COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tools/observe/desktop_matrix.py
                --executable $<TARGET_FILE:kearne>
                --mode smoke
    )
    set_tests_properties(desktop-observation-smoke PROPERTIES TIMEOUT 300)

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
                ${KEARNE_TEST_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/kearne/observe/desktop_matrix.py
                --executable ${KEARNE_TEST_INSTALL_PREFIX}/bin/kearne${CMAKE_EXECUTABLE_SUFFIX}
                --mode smoke
                --scenario workspace-model
                --scenario transient-command-palette
                --scenario settings-input
    )
    set_tests_properties(installed-desktop-observation PROPERTIES
        FIXTURES_REQUIRED installed-desktop
        TIMEOUT 60
    )
endif()
