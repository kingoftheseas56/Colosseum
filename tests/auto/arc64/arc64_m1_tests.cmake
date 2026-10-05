# Arc64 M1 registrations stay together to keep integration conflicts local.
find_program(COLOSSEUM_ARC64_NODE NAMES node)
if(COLOSSEUM_ARC64_NODE)
    add_test(NAME colosseum.abb_parse COMMAND "${COLOSSEUM_ARC64_NODE}" tests/abb_parse_test.mjs)
    set_tests_properties(colosseum.abb_parse PROPERTIES LABELS "unit"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.." TIMEOUT 120)
endif()

add_executable(tst_audiobook_index auto/audiobook/tst_audiobook_index.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/AudiobookDownloader.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/AudiobookDownloader.h"
    "${COLOSSEUM_NATIVE_DIR}/AudioPairingStore.h"
    "${COLOSSEUM_NATIVE_DIR}/player/streamserver.h")
target_include_directories(tst_audiobook_index PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_audiobook_index PRIVATE Qt6::Core Qt6::Network Qt6::Test)
set_target_properties(tst_audiobook_index PROPERTIES AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_audiobook_index POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>" "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.audiobook_index COMMAND tst_audiobook_index)
set_tests_properties(colosseum.qttest.audiobook_index PROPERTIES
    LABELS "unit;qttest" ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1" TIMEOUT 120)
