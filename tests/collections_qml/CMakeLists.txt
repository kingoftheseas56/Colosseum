cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ColosseumCollectionsQmlChecks LANGUAGES CXX)
    enable_testing()
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickTest)
get_filename_component(COLOSSEUM_COLLECTIONS_QML_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_executable(named_collections_qml_test
    runner.cpp
    "${COLOSSEUM_COLLECTIONS_QML_ROOT}/native/NamedCollectionsStore.cpp"
    "${COLOSSEUM_COLLECTIONS_QML_ROOT}/native/NamedCollectionsStore.h"
    "${COLOSSEUM_COLLECTIONS_QML_ROOT}/native/engine/ColosseumTitleIdentityRegistry.cpp"
    "${COLOSSEUM_COLLECTIONS_QML_ROOT}/native/engine/ColosseumTitleIdentityRegistry.h")
set_target_properties(named_collections_qml_test PROPERTIES
    AUTOMOC ON CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(named_collections_qml_test PRIVATE
    "${COLOSSEUM_COLLECTIONS_QML_ROOT}/native")
target_link_libraries(named_collections_qml_test PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickTest)
if(MSVC)
    target_compile_options(named_collections_qml_test PRIVATE /utf-8)
endif()

add_test(NAME colosseum.named_collections_qml_test
    COMMAND named_collections_qml_test -input "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(colosseum.named_collections_qml_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    LABELS "collections;qml;native-store;gui"
    TIMEOUT 180
    ENVIRONMENT "QT_QUICK_CONTROLS_STYLE=Basic;QML_DISABLE_DISK_CACHE=1;QT_FORCE_STDERR_LOGGING=1")
