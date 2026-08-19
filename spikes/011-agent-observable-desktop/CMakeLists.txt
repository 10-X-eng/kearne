cmake_minimum_required(VERSION 3.28)

project(kearne_observation_spike LANGUAGES CXX)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(kearne-observation-spike
    main.cpp
)

qt_add_qml_module(kearne-observation-spike
    URI Kearne.ObservationSpike
    VERSION 1.0
    NO_IMPORT_SCAN
    QML_FILES Main.qml
)

target_compile_features(kearne-observation-spike PRIVATE cxx_std_20)
target_link_libraries(kearne-observation-spike PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)
