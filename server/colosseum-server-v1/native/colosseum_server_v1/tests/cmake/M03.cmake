# Include after target colosseum_server_v1 is declared. No moc-generated sources.
target_sources(colosseum_server_v1 PRIVATE
    src/media/KeyframeIndex.cpp
    src/media/Mp4Samples.cpp
    src/media/MkvSamples.cpp)
# Existing server library already links Qt Core/Network.
add_executable(server1_test_samples tests/test_samples.cpp)
target_link_libraries(server1_test_samples PRIVATE colosseum_server_v1)
# Differential commands need the pinned oracle path and ffmpeg/node/python on PATH:
# python tests/run_samples_differential.py --candidate <server1_test_samples>
#   --oracle <locked server.js> --out <artifacts/server1/M03>
# python tests/run_samples_ranges.py --candidate <server1_test_samples>
#   --oracle <locked server.js> --out <artifacts/server1/M03>
