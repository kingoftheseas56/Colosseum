# Focused Android contracts compile real owners; no app/APK target is built.
function(colosseum_add_android_integration_tests)
    find_package(Qt6 REQUIRED COMPONENTS Gui Network Qml Quick)
    set(_root "${COLOSSEUM_NATIVE_DIR}/..")
    add_executable(tst_android_download_recovery
        "${_root}/tests/auto/player/tst_android_download_recovery.cpp"
        "${COLOSSEUM_NATIVE_DIR}/player/downloadstore.cpp"
        "${COLOSSEUM_NATIVE_DIR}/player/downloadstore.h")
    target_include_directories(tst_android_download_recovery PRIVATE "${COLOSSEUM_NATIVE_DIR}")
    target_link_libraries(tst_android_download_recovery PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test)
    add_executable(tst_android_back_contract
        "${_root}/tests/auto/player/tst_android_back_contract.cpp"
        "${COLOSSEUM_NATIVE_DIR}/platform/PlatformRuntime.cpp"
        "${COLOSSEUM_NATIVE_DIR}/platform/PlatformRuntime.h"
        "${COLOSSEUM_NATIVE_DIR}/platform/PlatformKind.cpp")
    target_include_directories(tst_android_back_contract PRIVATE "${COLOSSEUM_NATIVE_DIR}")
    target_compile_definitions(tst_android_back_contract PRIVATE COLOSSEUM_TEST_SOURCE_ROOT="${_root}")
    target_link_libraries(tst_android_back_contract PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
    add_executable(android_media3_state_harness "${_root}/tests/android_media3_state_harness.cpp" "${COLOSSEUM_NATIVE_DIR}/player/androidmedia3state.cpp")
    target_link_libraries(android_media3_state_harness PRIVATE Qt6::Core)
    add_executable(android_surface_redraw_gate_test "${_root}/tests/android_surface_redraw_gate_test.cpp")
    target_include_directories(android_surface_redraw_gate_test PRIVATE "${COLOSSEUM_NATIVE_DIR}")
    add_executable(subtitle_sidecar_parser_test "${_root}/tests/subtitle_sidecar_parser_test.cpp" "${COLOSSEUM_NATIVE_DIR}/player/subtitlesidecar.cpp")
    target_include_directories(subtitle_sidecar_parser_test PRIVATE "${COLOSSEUM_NATIVE_DIR}")
    target_link_libraries(subtitle_sidecar_parser_test PRIVATE Qt6::Core)
    # Compile the same production backend with NO fixture flag/header include path.
    # This separate archive catches accidental unguarded test-only APIs on host.
    add_library(nuvio_production_contract STATIC ${COLOSSEUM_NUVIO_BACKEND_SOURCES})
    target_compile_definitions(nuvio_production_contract PRIVATE COLOSSEUM_ANDROID_NUVIO_BACKEND=1)
    target_include_directories(nuvio_production_contract PRIVATE "${COLOSSEUM_NATIVE_DIR}" "${COLOSSEUM_NATIVE_DIR}/third_party/nuvio-engine/include")
    target_link_libraries(nuvio_production_contract PRIVATE Qt6::Core)
    foreach(_target tst_android_download_recovery tst_android_back_contract android_media3_state_harness android_surface_redraw_gate_test subtitle_sidecar_parser_test)
        add_test(NAME "colosseum.android.${_target}" COMMAND ${_target})
        set_tests_properties("colosseum.android.${_target}" PROPERTIES TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic")
    endforeach()
endfunction()
