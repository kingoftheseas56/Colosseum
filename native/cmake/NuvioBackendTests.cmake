# Focused tests for the opt-in Android Nuvio playback backend.
#
# The test target compiles the PRODUCTION adapter sources and the production
# Android facade translation unit (the shared
# COLOSSEUM_NUVIO_BACKEND_SOURCES list below) — there is no reimplemented
# fake backend algorithm; the engine side is a scripted C function-table
# provider injected through the production seam. Defining
# COLOSSEUM_ANDROID_NUVIO_BACKEND for this target compiles the facade on the
# host: honest conditional/syntax and unavailable-state coverage only, which
# does NOT establish Android JNI, ABI, or packaging behavior.
#
# Included both by tests/CMakeLists.txt (regular desktop builds) and by the
# minimal COLOSSEUM_NUVIO_BACKEND_TESTS_ONLY project branch in
# native/CMakeLists.txt, which returns before unrelated desktop dependencies.

set(COLOSSEUM_NUVIO_BACKEND_SOURCES
    "${COLOSSEUM_NATIVE_DIR}/player/nuvioengineapi.cpp"
    "${COLOSSEUM_NATIVE_DIR}/player/nuviostreambackend.cpp"
    "${COLOSSEUM_NATIVE_DIR}/player/nuvioengineapi.h"
    "${COLOSSEUM_NATIVE_DIR}/player/nuviostreambackend.h"
    "${COLOSSEUM_NATIVE_DIR}/player/streamserver.h"
    "${COLOSSEUM_NATIVE_DIR}/player/streamserver_android.cpp"
)

function(colosseum_add_nuvio_backend_tests)
    add_executable(
        tst_nuvio_stream_backend
        "${COLOSSEUM_NATIVE_DIR}/../tests/auto/player/tst_nuvio_stream_backend.cpp"
        ${COLOSSEUM_NUVIO_BACKEND_SOURCES}
    )
    target_compile_definitions(
        tst_nuvio_stream_backend
        PRIVATE COLOSSEUM_ANDROID_NUVIO_BACKEND=1
    )
    target_include_directories(
        tst_nuvio_stream_backend
        PRIVATE
            "${COLOSSEUM_NATIVE_DIR}"
            "${COLOSSEUM_NATIVE_DIR}/third_party/nuvio-engine/include"
            "${COLOSSEUM_NATIVE_DIR}/../tests"
    )
    target_link_libraries(tst_nuvio_stream_backend PRIVATE Qt6::Core Qt6::Test)
    set_target_properties(
        tst_nuvio_stream_backend
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/.."
    )
    if(WIN32)
        add_custom_command(
            TARGET tst_nuvio_stream_backend
            POST_BUILD
            COMMAND
                ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::Core>"
                "$<TARGET_FILE:Qt6::Test>"
                "$<TARGET_FILE_DIR:tst_nuvio_stream_backend>"
            VERBATIM
        )
    endif()
    add_test(
        NAME colosseum.qttest.nuvio_stream_backend
        COMMAND tst_nuvio_stream_backend
    )
    set_tests_properties(
        colosseum.qttest.nuvio_stream_backend
        PROPERTIES
            LABELS "unit;player;qttest;contract"
            ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
            TIMEOUT 120
    )
    add_executable(tst_nuvio_fixture_admission
        "${COLOSSEUM_NATIVE_DIR}/../tests/nuvio_fixture_admission_test.cpp")
    target_include_directories(tst_nuvio_fixture_admission PRIVATE "${COLOSSEUM_NATIVE_DIR}")
    target_link_libraries(tst_nuvio_fixture_admission PRIVATE Qt6::Core Qt6::Test)
    if(WIN32)
        add_custom_command(TARGET tst_nuvio_fixture_admission POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE:Qt6::Test>"
                "$<TARGET_FILE_DIR:tst_nuvio_fixture_admission>" VERBATIM)
    endif()
    add_test(NAME colosseum.qttest.nuvio_fixture_admission COMMAND tst_nuvio_fixture_admission)
    set_tests_properties(colosseum.qttest.nuvio_fixture_admission PROPERTIES
        LABELS "unit;player;qttest;contract" TIMEOUT 15)
endfunction()
