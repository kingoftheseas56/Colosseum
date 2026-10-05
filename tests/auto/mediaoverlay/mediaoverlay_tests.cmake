# No application linkage: these five units use only Core, Test and vendored miniz.
set(_mo_native "${COLOSSEUM_NATIVE_DIR}/reader2/mediaoverlay")
add_executable(tst_smil_clock auto/mediaoverlay/tst_smil_clock.cpp "${_mo_native}/SmilClock.cpp")
add_executable(tst_media_overlay_document auto/mediaoverlay/tst_media_overlay_document.cpp "${_mo_native}/MediaOverlayDocument.cpp" "${_mo_native}/SmilClock.cpp")
add_executable(tst_media_overlay_package auto/mediaoverlay/tst_media_overlay_package.cpp "${_mo_native}/MediaOverlayPackage.cpp" "${_mo_native}/SmilClock.cpp")
add_executable(tst_cue_timeline auto/mediaoverlay/tst_cue_timeline.cpp "${_mo_native}/CueTimeline.cpp")
add_executable(tst_epub_overlay_loader auto/mediaoverlay/tst_epub_overlay_loader.cpp
    "${_mo_native}/SmilClock.cpp" "${_mo_native}/MediaOverlayDocument.cpp"
    "${_mo_native}/MediaOverlayPackage.cpp" "${_mo_native}/CueTimeline.cpp"
    "${_mo_native}/EpubOverlayLoader.cpp" "${COLOSSEUM_NATIVE_DIR}/third_party/miniz/miniz.c")
target_compile_definitions(tst_epub_overlay_loader PRIVATE
    MEDIAOVERLAY_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mediaoverlay")
set(_mo_targets smil_clock media_overlay_document media_overlay_package cue_timeline epub_overlay_loader)
foreach(_mo_name IN LISTS _mo_targets)
    target_include_directories(tst_${_mo_name} PRIVATE "${COLOSSEUM_NATIVE_DIR}")
    target_link_libraries(tst_${_mo_name} PRIVATE Qt6::Core Qt6::Test)
    set_target_properties(tst_${_mo_name} PROPERTIES AUTOMOC ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
    add_custom_command(TARGET tst_${_mo_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>" "${CMAKE_CURRENT_BINARY_DIR}/..")
    add_test(NAME colosseum.qttest.${_mo_name} COMMAND tst_${_mo_name})
    set_tests_properties(colosseum.qttest.${_mo_name} PROPERTIES
        LABELS "unit;qttest" ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1" TIMEOUT 120)
endforeach()
unset(_mo_name)
unset(_mo_targets)
unset(_mo_native)
