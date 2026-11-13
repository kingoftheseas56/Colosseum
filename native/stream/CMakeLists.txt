# Pure deterministic source-picker library. It does not initiate playback or networking.
add_library(colosseum_stream_picker STATIC
    StreamTypes.cpp StreamParser.cpp StreamTrust.cpp SourceScorer.cpp)
target_include_directories(colosseum_stream_picker PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(colosseum_stream_picker PUBLIC Qt6::Core)
target_compile_features(colosseum_stream_picker PUBLIC cxx_std_17)
