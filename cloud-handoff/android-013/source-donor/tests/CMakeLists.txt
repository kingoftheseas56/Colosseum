add_executable(
    nuvio_engine_tests
    test_main.cpp
    byte_range_test.cpp
    ${PROJECT_SOURCE_DIR}/src/http/byte_range.cpp
    http_protocol_test.cpp
    ${PROJECT_SOURCE_DIR}/src/http/http_protocol.cpp
    loopback_server_test.cpp
    ${PROJECT_SOURCE_DIR}/src/http/loopback_server.cpp
    atomic_file_test.cpp
    ${PROJECT_SOURCE_DIR}/src/storage/atomic_file.cpp
    payload_reader_test.cpp
    ${PROJECT_SOURCE_DIR}/src/storage/payload_reader.cpp
    verified_piece_cache_test.cpp
    ${PROJECT_SOURCE_DIR}/src/cache/verified_piece_cache.cpp
    disk_cache_manager_test.cpp
    ${PROJECT_SOURCE_DIR}/src/cache/disk_cache_manager.cpp
    disk_cache_policy_integration_test.cpp
    engine_api_test.cpp
    file_selection_test.cpp
    ${PROJECT_SOURCE_DIR}/src/torrent/file_selection.cpp
    piece_scheduler_test.cpp
    ${PROJECT_SOURCE_DIR}/src/scheduler/piece_scheduler.cpp
    stream_demand_plan_test.cpp
    ${PROJECT_SOURCE_DIR}/src/scheduler/stream_demand_plan.cpp
    random_bytes_test.cpp
    ${PROJECT_SOURCE_DIR}/src/security/random_bytes.cpp
)

target_link_libraries(nuvio_engine_tests PRIVATE Nuvio::Engine)
target_compile_features(nuvio_engine_tests PRIVATE cxx_std_20)
target_include_directories(
    nuvio_engine_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(nuvio_engine_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(nuvio_engine_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_test(NAME nuvio_engine_tests COMMAND nuvio_engine_tests)

add_executable(nuvio_engine_c_api_smoke c_api_smoke.c)
target_link_libraries(nuvio_engine_c_api_smoke PRIVATE Nuvio::Engine)
add_test(NAME nuvio_engine_c_api_smoke COMMAND nuvio_engine_c_api_smoke)

if(WIN32)
    target_link_libraries(nuvio_engine_tests PRIVATE ws2_32 bcrypt)
endif()

if(TARGET torrent-rasterbar)
    # End-to-end loopback regression: real engine + fixture seeder + loopback
    # tracker; requires libtorrent headers, so it only builds with the
    # libtorrent backend enabled.
    add_executable(
        nuvio_engine_stream_integrity
        test_main.cpp
        stream_integrity_regression_test.cpp
    )
    target_link_libraries(
        nuvio_engine_stream_integrity
        PRIVATE
            Nuvio::Engine
            torrent-rasterbar
    )
    target_compile_features(nuvio_engine_stream_integrity PRIVATE cxx_std_20)
    target_compile_definitions(nuvio_engine_stream_integrity PRIVATE NUVIO_ENGINE_INTEGRITY_TEST_SEAM=1)
    target_include_directories(
        nuvio_engine_stream_integrity
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${PROJECT_SOURCE_DIR}/src
    )
    if(MSVC)
        target_compile_options(nuvio_engine_stream_integrity PRIVATE /W4 /permissive-)
    else()
        target_compile_options(
            nuvio_engine_stream_integrity
            PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        )
    endif()
    if(WIN32)
        target_link_libraries(nuvio_engine_stream_integrity PRIVATE ws2_32 bcrypt)
    endif()
    add_test(NAME nuvio_engine_stream_integrity COMMAND nuvio_engine_stream_integrity)
    set_tests_properties(
        nuvio_engine_stream_integrity
        PROPERTIES TIMEOUT 300
    )
endif()
