# Integration owner only: add these sources to the existing media library target.
# src/media/HlsPlaylist.cpp
# src/media/SegmentLayout.cpp
# src/media/Mp4Timeline.cpp
# src/media/VttTimeline.cpp
# SegmentLayout.cpp depends on M03 src/media/KeyframeIndex.cpp.
# HlsPlaylist.cpp requires the existing pinned Boost header include authority.
# No Boost library is needed (multiprecision is header-only).
add_executable(server1_m04_timeline_tests tests/test_segment_timeline.cpp)
target_link_libraries(server1_m04_timeline_tests PRIVATE colosseum_server_v1 Qt6::Core)
add_test(NAME M04-01-segment-timeline
  COMMAND server1_m04_timeline_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/segment-timeline.json")
# When adding inside tests/CMakeLists.txt, omit tests/ from fixture path above.
