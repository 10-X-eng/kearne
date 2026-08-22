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
set(KEARNE_SKETCH_VERTEX_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sketch_scene.vert")
set(KEARNE_SKETCH_VERTEX_OUTPUT
    "${KEARNE_SKETCH_SHADER_OUTPUT_DIR}/sketch_scene.vert.qsb")
add_custom_command(
    OUTPUT "${KEARNE_SKETCH_VERTEX_OUTPUT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${KEARNE_SKETCH_SHADER_OUTPUT_DIR}"
    COMMAND "${KEARNE_QT6_QSB_EXECUTABLE}"
            --glsl "310 es,430" --hlsl 50 --msl 12 -O
            -o "${KEARNE_SKETCH_VERTEX_OUTPUT}"
            "${KEARNE_SKETCH_VERTEX_SOURCE}"
    DEPENDS "${KEARNE_SKETCH_VERTEX_SOURCE}"
    VERBATIM
)
set_source_files_properties("${KEARNE_SKETCH_VERTEX_OUTPUT}" PROPERTIES
    GENERATED TRUE QT_RESOURCE_ALIAS "sketch_scene.vert.qsb")
list(APPEND KEARNE_SKETCH_SHADER_OUTPUTS "${KEARNE_SKETCH_VERTEX_OUTPUT}")

set(KEARNE_SKETCH_FRAGMENT_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sketch_scene.frag")
function(kearne_add_sketch_fragment family)
    set(output
        "${KEARNE_SKETCH_SHADER_OUTPUT_DIR}/sketch_scene_${family}.frag.qsb")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${KEARNE_SKETCH_SHADER_OUTPUT_DIR}"
        COMMAND "${KEARNE_QT6_QSB_EXECUTABLE}"
                --glsl "310 es,430" --hlsl 50 --msl 12 -O
                ${ARGN}
                -o "${output}" "${KEARNE_SKETCH_FRAGMENT_SOURCE}"
        DEPENDS "${KEARNE_SKETCH_FRAGMENT_SOURCE}"
        VERBATIM
    )
    set_source_files_properties("${output}" PROPERTIES
        GENERATED TRUE
        QT_RESOURCE_ALIAS "sketch_scene_${family}.frag.qsb"
    )
    list(APPEND KEARNE_SKETCH_SHADER_OUTPUTS "${output}")
    set(KEARNE_SKETCH_SHADER_OUTPUTS "${KEARNE_SKETCH_SHADER_OUTPUTS}"
        PARENT_SCOPE)
endfunction()
kearne_add_sketch_fragment(basic -D KEARNE_BASIC_ONLY=1)
kearne_add_sketch_fragment(annotation
    -D KEARNE_BASIC_ONLY=1 -D KEARNE_ANNOTATION_ONLY=1)
kearne_add_sketch_fragment(nurbs_low_degree
    -D KEARNE_NURBS_ONLY=1 -D KEARNE_NURBS_MAX_DEGREE=3)
kearne_add_sketch_fragment(nurbs_general -D KEARNE_NURBS_ONLY=1)

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
    src/sketch_dash_pattern.hpp
    src/sketch_vector_packet.cpp
    src/sketch_vector_packet.hpp
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
    src/sketch_constraint_markers.cpp
    src/sketch_constraint_markers.hpp
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
        Kearne::SketchOcc
        Kearne::SketchSourceWorker
        Kearne::SketchWorkflow
)

add_library(kearne_desktop_frontend STATIC
    src/command_catalog.hpp
    src/desktop_controller.cpp
    src/desktop_controller.hpp
    src/frontend_contract.hpp
    src/sketch_gesture_preview.cpp
    src/sketch_tool_gesture.cpp
    src/sketch_gesture_preview.hpp
    src/sketch_tool_gesture.hpp
    src/sketch_provisional_projection.cpp
    src/sketch_provisional_projection.hpp
)
add_library(Kearne::DesktopFrontend ALIAS kearne_desktop_frontend)
target_include_directories(kearne_desktop_frontend PUBLIC src)
target_link_libraries(kearne_desktop_frontend
    PUBLIC Kearne::LocalSketchSession Qt6::Core
    PRIVATE Kearne::BuildOptions Kearne::SketchOcc
)

qt_add_executable(kearne
    src/application_context.cpp
    src/application_context.hpp
    src/engineering_grid_item.cpp
    src/engineering_grid_item.hpp
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
    src/sketch_viewport_bridge.cpp
    src/sketch_viewport_bridge.hpp
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
        qml/components/CommandFamily.qml
        qml/components/CommandStrip.qml
        qml/components/StructurePanel.qml
        qml/components/EngineeringGridScale.qml
        qml/components/EngineeringGrid.qml
        qml/components/BoxEdges3D.qml
        qml/components/NavigationCalibrationScene.qml
        qml/components/DatumPlaneSelector.qml
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

get_property(kearne_desktop_generated_python_root GLOBAL PROPERTY
    KEARNE_API_GENERATED_PYTHON_ROOT)
set(kearne_desktop_python_runtime_root
    "${CMAKE_CURRENT_BINARY_DIR}/python")
file(GLOB_RECURSE kearne_desktop_python_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/sdk/python/src/*.py")
add_custom_target(kearne_desktop_python_runtime ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf
            "${kearne_desktop_python_runtime_root}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${kearne_desktop_python_runtime_root}/sdk"
            "${kearne_desktop_python_runtime_root}/generated"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/sdk/python/src"
            "${kearne_desktop_python_runtime_root}/sdk"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${kearne_desktop_generated_python_root}"
            "${kearne_desktop_python_runtime_root}/generated"
    DEPENDS kearne_api_metadata ${kearne_desktop_python_sources}
    COMMENT "Staging Kearne Python runtime"
    VERBATIM
)
add_dependencies(kearne kearne_desktop_python_runtime)
target_compile_definitions(kearne PRIVATE
    KEARNE_VERSION="${PROJECT_VERSION}"
    KEARNE_BUILD_TYPE="$<CONFIG>"
    "KEARNE_LOCAL_PYTHON=\"$<TARGET_FILE:Python3::Interpreter>\""
)

target_include_directories(kearne PRIVATE src)

target_link_libraries(kearne PRIVATE
    Kearne::BuildOptions
    Kearne::DesktopFrontend
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
install(DIRECTORY "${kearne_desktop_python_runtime_root}/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/kearne/python")
install(PROGRAMS ${PROJECT_SOURCE_DIR}/tools/observe/desktop_matrix.py
        DESTINATION ${CMAKE_INSTALL_DATADIR}/kearne/observe)
install(FILES ${PROJECT_SOURCE_DIR}/tools/observe/observation.schema.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/kearne/observe)

if(BUILD_TESTING)
    get_property(kearne_desktop_generated_python_root GLOBAL PROPERTY
        KEARNE_API_GENERATED_PYTHON_ROOT)
    function(kearne_configure_local_sketch_test target)
        target_link_libraries(${target} PRIVATE
            Kearne::DesktopFrontend Kearne::LocalSketchSession Kearne::Testkit
            Kearne::BuildOptions Qt6::Core)
        target_compile_definitions(${target} PRIVATE
            "KEARNE_TEST_BUILD_TYPE=\"$<CONFIG>\""
            "KEARNE_TEST_PYTHON=\"$<TARGET_FILE:Python3::Interpreter>\""
            "KEARNE_TEST_SDK_ROOT=\"${PROJECT_SOURCE_DIR}/sdk/python/src\""
            "KEARNE_TEST_GENERATED_PYTHON_ROOT=\"${kearne_desktop_generated_python_root}\"")
    endfunction()

    add_executable(kearne_local_sketch_session_contract_tests
        tests/local_sketch_session_contract.cpp
    )
    kearne_configure_local_sketch_test(
        kearne_local_sketch_session_contract_tests)
    add_test(NAME local-sketch-session-contracts
             COMMAND kearne_local_sketch_session_contract_tests)
    set_tests_properties(local-sketch-session-contracts PROPERTIES
        LABELS "desktop;sketch;workflow;contract;concurrency"
        TIMEOUT 60)

    add_executable(kearne_sketch_provisional_projection_contract_tests
        tests/sketch_provisional_projection_contract.cpp
        tests/sketch_scene_fixture.hpp
    )
    target_link_libraries(
        kearne_sketch_provisional_projection_contract_tests PRIVATE
        Kearne::DesktopFrontend
        Kearne::Testkit
        Kearne::BuildOptions
        Qt6::Core
        Qt6::Gui
    )
    add_test(NAME sketch-provisional-projection-contracts
             COMMAND kearne_sketch_provisional_projection_contract_tests)
    set_tests_properties(sketch-provisional-projection-contracts PROPERTIES
        LABELS "desktop;sketch;render;contract;generated"
        TIMEOUT 60)

    add_executable(kearne_local_sketch_session_benchmark
        tests/local_sketch_session_benchmark.cpp)
    kearne_configure_local_sketch_test(
        kearne_local_sketch_session_benchmark)
    add_test(NAME local-sketch-session-performance
             COMMAND kearne_local_sketch_session_benchmark)
    set_tests_properties(local-sketch-session-performance PROPERTIES
        LABELS "desktop;sketch;workflow;performance"
        RUN_SERIAL TRUE
        TIMEOUT 30)

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
        find_program(KEARNE_XVFB_RUN xvfb-run REQUIRED)
    endif()
    function(kearne_add_native_render_tests target prefix)
        foreach(backend IN LISTS KEARNE_NATIVE_RENDER_BACKENDS)
            set(environment
                QT_QPA_PLATFORM=offscreen
                QT_QUICK_BACKEND=rhi
                QT_SCALE_FACTOR=1
            )
            set(command ${target} --backend ${backend})
            if(UNIX AND NOT APPLE AND backend STREQUAL "vulkan")
                set(environment
                    QT_QPA_PLATFORM=xcb
                    QT_QUICK_BACKEND=rhi
                    QT_SCALE_FACTOR=1
                )
                set(command
                    ${KEARNE_XVFB_RUN} -a
                    $<TARGET_FILE:${target}> --backend ${backend})
            endif()
            if(KEARNE_ENABLE_SANITIZERS AND UNIX AND NOT APPLE AND
               backend STREQUAL "opengl")
                list(APPEND environment
                    "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tests/nvidia-opengl-lsan.supp")
            endif()
            add_test(NAME ${prefix}-${backend} COMMAND ${command})
            set_tests_properties(${prefix}-${backend} PROPERTIES
                ENVIRONMENT "${environment}"
                LABELS "render;native;contract"
                TIMEOUT 60
            )
            if(NOT (UNIX AND NOT APPLE AND backend STREQUAL "vulkan"))
                set_tests_properties(${prefix}-${backend} PROPERTIES
                    SKIP_RETURN_CODE 77)
            endif()
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

    add_executable(kearne_sketch_scene_item_benchmark
        tests/sketch_scene_item_benchmark.cpp
        tests/sketch_scene_fixture.hpp
    )
    target_link_libraries(kearne_sketch_scene_item_benchmark PRIVATE
        Kearne::SketchSceneQuick
        Kearne::Testkit
        Kearne::BuildOptions
    )
    add_test(NAME sketch-scene-vector-performance
             COMMAND kearne_sketch_scene_item_benchmark)
    set_tests_properties(sketch-scene-vector-performance PROPERTIES
        LABELS "performance;render;sketch"
        TIMEOUT 120)

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

    qt_add_executable(kearne_desktop_controller_contract_tests
        tests/desktop_controller_contract.cpp
    )
    target_link_libraries(kearne_desktop_controller_contract_tests PRIVATE
        Kearne::DesktopFrontend Qt6::Core)
    target_link_libraries(kearne_desktop_controller_contract_tests PRIVATE
        Kearne::BuildOptions)
    add_test(NAME desktop-controller-contracts
             COMMAND kearne_desktop_controller_contract_tests)
    set_tests_properties(desktop-controller-contracts PROPERTIES TIMEOUT 30)

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

    add_test(
        NAME installed-design-engine-observation
        COMMAND ${Python3_EXECUTABLE}
                ${KEARNE_TEST_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/kearne/observe/desktop_matrix.py
                --executable ${KEARNE_TEST_INSTALL_PREFIX}/bin/kearne${CMAKE_EXECUTABLE_SUFFIX}
                --mode proof
                --scenario proof-sketch-plane-preselection
    )
    set_tests_properties(installed-design-engine-observation PROPERTIES
        FIXTURES_REQUIRED installed-desktop
        LABELS "desktop;worker;workflow;contract"
        TIMEOUT 60
    )
endif()
