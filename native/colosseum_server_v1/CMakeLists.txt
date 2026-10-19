cmake_minimum_required(VERSION 3.24)
project(colosseum_server_v1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The accepted Windows dependency lock remains authoritative on Windows.
# Linux uses an explicitly selected native 2.0.11 package; its provenance is
# recorded separately and is never represented as the locked MSVC archive.
if(WIN32)
  set(P03_DEPENDENCY_LOCK "${CMAKE_CURRENT_SOURCE_DIR}/../../docs/server1/DEPENDENCY-LOCK.json" CACHE FILEPATH
      "Accepted Server 1.0 dependency lock")
  if(NOT EXISTS "${P03_DEPENDENCY_LOCK}")
    message(FATAL_ERROR "P03 required dependency lock is unavailable: ${P03_DEPENDENCY_LOCK}")
  endif()

  file(READ "${P03_DEPENDENCY_LOCK}" P03_LOCK_JSON)
  string(JSON P03_LIBTORRENT_ROOT GET "${P03_LOCK_JSON}" libtorrent package_root)
  string(JSON P03_LIBTORRENT_ARCHIVE GET "${P03_LOCK_JSON}" libtorrent archive_identity path)
  string(JSON P03_BOOST_ROOT GET "${P03_LOCK_JSON}" boost include_root)
  string(JSON P03_OPENSSL_ROOT GET "${P03_LOCK_JSON}" openssl root)
  string(JSON P03_QT_GLOBAL_HEADER GET "${P03_LOCK_JSON}" external_files 10 path)
  cmake_path(GET P03_QT_GLOBAL_HEADER PARENT_PATH P03_QT_CORE_INCLUDE)
  cmake_path(GET P03_QT_CORE_INCLUDE PARENT_PATH P03_QT_INCLUDE)
  cmake_path(GET P03_QT_INCLUDE PARENT_PATH P03_QT_ROOT)

  set(P03_REQUIRED_FILES
    "${P03_LIBTORRENT_ROOT}/include/libtorrent/version.hpp"
    "${P03_LIBTORRENT_ARCHIVE}"
    "${P03_BOOST_ROOT}/boost/version.hpp"
    "${P03_OPENSSL_ROOT}/include/openssl/opensslv.h"
    "${P03_OPENSSL_ROOT}/lib/libssl.lib"
    "${P03_OPENSSL_ROOT}/lib/libcrypto.lib"
    "${P03_QT_GLOBAL_HEADER}"
  )
  foreach(P03_REQUIRED_FILE IN LISTS P03_REQUIRED_FILES)
    if(NOT EXISTS "${P03_REQUIRED_FILE}")
      message(FATAL_ERROR "P03 locked dependency unavailable: ${P03_REQUIRED_FILE}")
    endif()
  endforeach()

  list(PREPEND CMAKE_PREFIX_PATH
    "${P03_QT_ROOT}"
    "${P03_LIBTORRENT_ROOT}"
  )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(LibtorrentRasterbar 2.0.11 EXACT CONFIG REQUIRED)
  find_package(Threads REQUIRED)
  get_target_property(SERVER1_LT_TYPE LibtorrentRasterbar::torrent-rasterbar TYPE)
  if(NOT SERVER1_LT_TYPE STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR
      "Server 1 requires a static libtorrent 2.0.11 Linux build: the adapter uses "
      "internal peer symbols hidden by distro shared libraries. Set "
      "LibtorrentRasterbar_DIR to the qualified static package.")
  endif()
  get_target_property(SERVER1_LT_CONFIGURATIONS LibtorrentRasterbar::torrent-rasterbar IMPORTED_CONFIGURATIONS)
  string(TOUPPER "${CMAKE_BUILD_TYPE};${CMAKE_CONFIGURATION_TYPES}" SERVER1_CONSUMER_CONFIGURATIONS)
  if(SERVER1_LT_CONFIGURATIONS STREQUAL "RELEASE" AND "DEBUG" IN_LIST SERVER1_CONSUMER_CONFIGURATIONS)
    message(FATAL_ERROR "Pinned libtorrent 2.0.11 provides only a Release ABI; Debug enables TORRENT_USE_ASSERTS and is incompatible. Select Release.")
  endif()
  include(CheckCXXSourceCompiles)
  set(SERVER1_SAVED_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES}")
  set(SERVER1_SAVED_TRY_COMPILE_TARGET_TYPE "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
  set(CMAKE_REQUIRED_LIBRARIES LibtorrentRasterbar::torrent-rasterbar)
  set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
  unset(SERVER1_LT_INTERNAL_LINK CACHE)
  check_cxx_source_compiles("#include <libtorrent/peer_connection.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/peer_info.hpp>
int main(int argc, char **argv) {
  if (argc == 12345) {
    auto *peer = reinterpret_cast<libtorrent::peer_connection *>(argv);
    peer->send_interested(); peer->send_choke(); peer->clear_request_queue();
    auto *torrent = reinterpret_cast<libtorrent::torrent *>(argv);
    auto *candidate = torrent->add_peer(libtorrent::tcp::endpoint{}, libtorrent::peer_info::tracker, libtorrent::pex_flags_t{});
    torrent->connect_to_peer(candidate, true);
  }
  return 0;
}" SERVER1_LT_INTERNAL_LINK)
  set(CMAKE_REQUIRED_LIBRARIES "${SERVER1_SAVED_REQUIRED_LIBRARIES}")
  if(SERVER1_SAVED_TRY_COMPILE_TARGET_TYPE)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE "${SERVER1_SAVED_TRY_COMPILE_TARGET_TYPE}")
  else()
    unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
  endif()
  if(NOT SERVER1_LT_INTERNAL_LINK)
    message(FATAL_ERROR "libtorrent 2.0.11 internal peer link probe failed")
  endif()
else()
  message(FATAL_ERROR "Server 1 build supports the locked Windows kit and native Linux only")
endif()
find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Network Concurrent)

add_library(server1_runtime INTERFACE)
target_include_directories(server1_runtime INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(server1_runtime INTERFACE Qt6::Core)

add_executable(server1_host host/main.cpp)
target_link_libraries(server1_host PRIVATE server1_runtime)

include(CTest)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/K00.cmake")
server1_register_k00_packet()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/K01.cmake")
server1_register_k01_packet()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/K13.cmake")
server1_register_k13_packet()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/H00.cmake")
server1_register_h00_packet()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/M00.cmake")
server1_register_m00_packet()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/M01.cmake")
server1_register_m01_packet()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/K02.cmake")
server1_register_k02_packet()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/K05.cmake")
server1_register_k05_packet()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/K06.cmake")
server1_register_k06_packet()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/K03.cmake")
server1_register_k03_packet()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/K07.cmake")
server1_register_k07_packet()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/K09.cmake")
server1_register_k09_packet()
add_library(server1_p08_torrent_transport_contract INTERFACE)
target_include_directories(server1_p08_torrent_transport_contract INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(server1_p08_torrent_transport_contract INTERFACE
  server1_k09_peer_search)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/K04.cmake")
server1_register_k04_packet()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/K08.cmake")
server1_register_k08_packet()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/K10.cmake")
server1_register_k10_packet()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/K11.cmake")
server1_register_k11_packet()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/K12.cmake")
server1_register_k12_packet()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/P03.cmake")
server1_register_p03_tests()

add_library(server1_h01_reader_stream STATIC src/http/FileReaderStream.cpp)
target_include_directories(server1_h01_reader_stream PUBLIC include)
target_link_libraries(server1_h01_reader_stream PUBLIC server1_h00_http server1_k08_file_reader)
if(BUILD_TESTING)
  add_executable(server1_h01_reader_stream_test tests/test_http_file_reader.cpp)
  target_link_libraries(server1_h01_reader_stream_test PRIVATE server1_h01_reader_stream)
  add_test(NAME H01-reader-stream COMMAND server1_h01_reader_stream_test)
endif()
target_sources(server1_h01_reader_stream PRIVATE src/http/TorrentRange.cpp)
if(BUILD_TESTING)
  add_executable(server1_h01_range_test tests/test_torrent_range.cpp)
  target_link_libraries(server1_h01_range_test PRIVATE server1_h01_reader_stream)
  add_test(NAME H01-torrent-range COMMAND server1_h01_range_test)
endif()
add_library(colosseum_server_v1 STATIC src/ServerComposition.cpp src/http/TorrentRoutes.cpp)
target_include_directories(colosseum_server_v1 PUBLIC include)
target_link_libraries(colosseum_server_v1 PUBLIC server1_h01_reader_stream server1_k11_engine Qt6::Network Qt6::Concurrent)
target_link_libraries(server1_host PRIVATE colosseum_server_v1)
target_sources(server1_k11_engine PRIVATE src/policy/EngineStatistics.cpp src/policy/SwarmConnections.cpp)
if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME H01-native-torrent-http COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_torrent_http.py"
    --host "$<TARGET_FILE:server1_host>"
    --peer "${CMAKE_CURRENT_SOURCE_DIR}/../../artifacts/server1/P08/P08-A/tiny_peer.py"
    --work "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(H01-native-torrent-http PROPERTIES TIMEOUT 45 LABELS "server1;native;http;wire")
  add_executable(server1_engine_statistics_test tests/test_engine_statistics.cpp)
  target_link_libraries(server1_engine_statistics_test PRIVATE server1_k11_engine)
  add_test(NAME K12-engine-statistics COMMAND server1_engine_statistics_test)
endif()

# Source-owned network discovery runs on the composition's Qt application lane.
target_sources(server1_k09_peer_search PRIVATE src/discovery/TrackerNetwork.cpp src/discovery/DhtLookup.cpp src/discovery/QtDiscoveryNetwork.cpp)
target_link_libraries(server1_k09_peer_search PUBLIC Qt6::Network)
if(BUILD_TESTING)
  foreach(component tracker_network dht_network dht_service discovery_network)
    add_executable(server1_${component}_test tests/test_${component}.cpp)
    target_link_libraries(server1_${component}_test PRIVATE server1_k09_peer_search)
    add_test(NAME K09-${component} COMMAND server1_${component}_test)
  endforeach()
  add_test(NAME H01-tracker-torrent-http COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_torrent_http.py"
    --host "$<TARGET_FILE:server1_host>"
    --peer "${CMAKE_CURRENT_SOURCE_DIR}/../../artifacts/server1/P08/P08-A/tiny_peer.py"
    --work "${CMAKE_CURRENT_BINARY_DIR}" --discovery)
  set_tests_properties(H01-tracker-torrent-http PROPERTIES TIMEOUT 45 LABELS "server1;native;http;wire;discovery")
endif()
if(BUILD_TESTING AND SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
  add_test(NAME K09-peer-duplicates-differential COMMAND "${SERVER1_NODE_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_peer_search_duplicates.cjs"
    "${SERVER1_K12_ORACLE}" "$<TARGET_FILE:server1_k09_peer_search_test>")
  add_test(NAME K09-dht-differential COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_dht_differential.py"
    --oracle "${SERVER1_K12_ORACLE}" --candidate "$<TARGET_FILE:server1_dht_service_test>"
    --node "${SERVER1_NODE_EXECUTABLE}")
endif()

find_package(ZLIB REQUIRED)
find_library(SERVER1_BROTLI_DECODER NAMES brotlidec REQUIRED)
add_library(server1_n00_proxy STATIC src/network/ProxyService.cpp src/network/ProxyRoutes.cpp)
target_include_directories(server1_n00_proxy PUBLIC include)
target_link_libraries(server1_n00_proxy PUBLIC server1_h00_http Qt6::Network ZLIB::ZLIB "${SERVER1_BROTLI_DECODER}")
target_link_libraries(colosseum_server_v1 PUBLIC server1_n00_proxy)
if(BUILD_TESTING)
  add_executable(server1_proxy_test tests/test_proxy_streaming.cpp)
  target_link_libraries(server1_proxy_test PRIVATE server1_n00_proxy)
  add_test(NAME N00-streaming-proxy COMMAND server1_proxy_test)
  add_executable(server1_k12_swarm_connections_test tests/test_swarm_connections.cpp)
  target_link_libraries(server1_k12_swarm_connections_test PRIVATE server1_k11_engine)
  add_test(NAME K12-source-swarm-connections COMMAND server1_k12_swarm_connections_test)
  if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
    add_test(NAME N00-proxy-differential COMMAND "${SERVER1_NODE_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_proxy_differential.cjs"
      "${SERVER1_K12_ORACLE}" "$<TARGET_FILE:server1_proxy_test>")
    set_tests_properties(N00-proxy-differential PROPERTIES TIMEOUT 60)
    add_test(NAME K12-source-swarm-differential COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/server_lab/check_swarm_connections.py"
      --oracle "${SERVER1_K12_ORACLE}" --candidate "$<TARGET_FILE:server1_k12_swarm_connections_test>")
  endif()
endif()

find_package(Iconv REQUIRED)
add_library(server1_m02_subtitles STATIC
  src/media/SubtitleFetch.cpp src/media/SubtitleCharset.cpp src/media/SubtitleHash.cpp
  src/media/TrackExtraction.cpp src/media/SubtitleRoutes.cpp)
target_include_directories(server1_m02_subtitles PUBLIC include)
target_link_libraries(server1_m02_subtitles PUBLIC server1_h00_http Qt6::Network ZLIB::ZLIB Iconv::Iconv)
target_link_libraries(colosseum_server_v1 PUBLIC server1_m02_subtitles)
if(BUILD_TESTING)
  add_executable(server1_m02_subtitles_tracks_tests tests/test_subtitles_tracks.cpp)
  add_executable(server1_m02_subtitles_lifecycle_tests tests/test_subtitles_lifecycle.cpp)
  target_link_libraries(server1_m02_subtitles_tracks_tests PRIVATE server1_m02_subtitles)
  target_link_libraries(server1_m02_subtitles_lifecycle_tests PRIVATE server1_m02_subtitles)
  foreach(cases oracle-cases charset-cases)
    add_test(NAME M02-${cases} COMMAND server1_m02_subtitles_tracks_tests --cases
      "${CMAKE_CURRENT_SOURCE_DIR}/../../artifacts/server1/M02/${cases}.json")
  endforeach()
  set(SERVER1_M02_OPTIONAL_ORACLE)
  if(SERVER1_K12_ORACLE)
    list(APPEND SERVER1_M02_OPTIONAL_ORACLE "${SERVER1_K12_ORACLE}")
  endif()
  add_test(NAME M02-lifecycle COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_subtitles_lifecycle.py"
    "$<TARGET_FILE:server1_m02_subtitles_lifecycle_tests>"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../artifacts/server1/M01/media"
    "${CMAKE_CURRENT_BINARY_DIR}/m02-evidence" ${SERVER1_M02_OPTIONAL_ORACLE})
  set_tests_properties(M02-lifecycle PROPERTIES TIMEOUT 180)
endif()

# M03/M04 native media indexing, playlists and fragmented MP4 timelines.
target_sources(colosseum_server_v1 PRIVATE
  src/media/KeyframeIndex.cpp src/media/Mp4Samples.cpp src/media/MkvSamples.cpp
  src/media/HlsPlaylist.cpp src/media/SegmentLayout.cpp src/media/Mp4Timeline.cpp
  src/media/VttTimeline.cpp)
if(WIN32)
  target_include_directories(colosseum_server_v1 PRIVATE "${P03_BOOST_ROOT}")
else()
  target_include_directories(colosseum_server_v1 PRIVATE "${Boost_INCLUDE_DIR}")
endif()
if(BUILD_TESTING)
  add_executable(server1_test_samples tests/test_samples.cpp)
  target_link_libraries(server1_test_samples PRIVATE colosseum_server_v1)
  add_executable(server1_m04_timeline_tests tests/test_segment_timeline.cpp)
  target_link_libraries(server1_m04_timeline_tests PRIVATE colosseum_server_v1)
  add_test(NAME M04-segment-timeline COMMAND server1_m04_timeline_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/segment-timeline.json")
  if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
    foreach(mode differential ranges fallback)
      add_test(NAME M03-samples-${mode} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_samples_${mode}.py"
        --candidate "$<TARGET_FILE:server1_test_samples>" --oracle "${SERVER1_K12_ORACLE}"
        --out "${CMAKE_CURRENT_BINARY_DIR}/m03-evidence")
      set_tests_properties(M03-samples-${mode} PROPERTIES TIMEOUT 180)
    endforeach()
    set_tests_properties(M03-samples-differential PROPERTIES FIXTURES_SETUP M03SamplesCorpus)
    set_tests_properties(M03-samples-ranges M03-samples-fallback PROPERTIES FIXTURES_REQUIRED M03SamplesCorpus)
  endif()
  add_test(NAME K12-incoming-lifecycle COMMAND server1_k10_native_transport_test
    --incoming-lifecycle "${CMAKE_CURRENT_BINARY_DIR}/k12-incoming")
  set_tests_properties(K12-incoming-lifecycle PROPERTIES TIMEOUT 45
    ENVIRONMENT "SERVER1_TEST_FIXED_INTERFACE=1" LABELS "server1;native;wire")
endif()

# Standalone ECMAScript regex C library: deliberately no JS runtime/interpreter.
add_library(server1_ecmascript_regex STATIC
  third_party/ecmascript_regex/libregexp.c third_party/ecmascript_regex/libunicode.c
  third_party/ecmascript_regex/cutils.c)
target_include_directories(server1_ecmascript_regex PUBLIC third_party/ecmascript_regex)
set_target_properties(server1_ecmascript_regex PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
if(MSVC)
  target_compile_options(server1_ecmascript_regex PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/third_party/ecmascript_regex/MsvcCompat.h")
endif()
add_library(server1_safe_file_regex STATIC src/http/SafeFileRegex.cpp)
target_include_directories(server1_safe_file_regex PUBLIC include)
target_link_libraries(server1_safe_file_regex PUBLIC Qt6::Core PRIVATE server1_ecmascript_regex)
target_link_libraries(colosseum_server_v1 PUBLIC server1_safe_file_regex)
if(BUILD_TESTING)
  add_executable(server1_safe_file_regex_test tests/test_safe_file_regex.cpp)
  target_link_libraries(server1_safe_file_regex_test PRIVATE server1_safe_file_regex)
  if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
    get_filename_component(SERVER1_ORACLE_DIRECTORY "${SERVER1_K12_ORACLE}" DIRECTORY)
    add_test(NAME H01-safe-regex-differential COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_safe_file_regex.py"
      --candidate "$<TARGET_FILE:server1_safe_file_regex_test>" --oracle "${SERVER1_K12_ORACLE}"
      --index "${SERVER1_ORACLE_DIRECTORY}/../../../MODULE-INDEX.json" --node "${SERVER1_NODE_EXECUTABLE}")
    set_tests_properties(H01-safe-regex-differential PROPERTIES TIMEOUT 45)
  endif()
endif()

# Local HLS conversion and the external-router origin policy.
target_sources(colosseum_server_v1 PRIVATE src/media/FfmpegArguments.cpp
  src/media/TrackConverter.cpp src/media/HlsV2Service.cpp src/http/HlsV2Routes.cpp
  src/http/ExternalCors.cpp)
target_link_libraries(colosseum_server_v1 PUBLIC server1_m00_process server1_m01_probe)
if(BUILD_TESTING)
  foreach(name track_converter track_converter_lifecycle track_converter_backpressure)
    add_executable(server1_${name}_tests tests/test_${name}.cpp)
    target_link_libraries(server1_${name}_tests PRIVATE colosseum_server_v1)
  endforeach()
  add_test(NAME M05-arguments COMMAND server1_track_converter_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/track-converter.json")
  add_test(NAME M05-lifecycle COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_track_converter_lifecycle.py"
    "$<TARGET_FILE:server1_track_converter_lifecycle_tests>"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/track-converter-lifecycle.json")
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME M05-owned-pipe-backpressure COMMAND server1_track_converter_backpressure_tests)
    set_tests_properties(M05-owned-pipe-backpressure PROPERTIES TIMEOUT 45)
  endif()
  add_executable(server1_m06_hlsv2_tests tests/test_hlsv2_service.cpp)
  target_link_libraries(server1_m06_hlsv2_tests PRIVATE colosseum_server_v1)
  include("${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/M06.cmake")
  server1_register_m06_tests()
  add_executable(server1_external_cors_tests tests/test_external_cors.cpp)
  target_link_libraries(server1_external_cors_tests PRIVATE colosseum_server_v1)
  add_test(NAME M06-external-cors COMMAND server1_external_cors_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/external-cors.json")
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME H01-torrent-hls-http COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_torrent_hls.py"
      --host "$<TARGET_FILE:server1_host>"
      --peer "${CMAKE_CURRENT_SOURCE_DIR}/../../artifacts/server1/P08/P08-A/tiny_peer.py"
      --media "${CMAKE_CURRENT_SOURCE_DIR}/../../artifacts/server1/M01/media/tiny.mp4"
      --work "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(H01-torrent-hls-http PROPERTIES TIMEOUT 60)
  endif()
endif()

# Node's system-statistics primitive uses these exact native libuv APIs.
include(FetchContent)
set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(LIBUV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBUV_BUILD_BENCH OFF CACHE BOOL "" FORCE)
FetchContent_Declare(server1_libuv
  URL https://dist.libuv.org/dist/v1.52.1/libuv-v1.52.1.tar.gz
  URL_HASH SHA256=66d511b9e6e334c0e62279eb234fbfb2b3110b1479c09b95b44c7afca8cff9e7
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(server1_libuv)
add_library(server1_system_statistics STATIC src/http/SystemStatistics.cpp)
target_include_directories(server1_system_statistics PUBLIC include)
target_link_libraries(server1_system_statistics PUBLIC server1_k00_policy PRIVATE uv_a)
target_link_libraries(colosseum_server_v1 PRIVATE server1_system_statistics)
if(BUILD_TESTING)
  add_executable(server1_system_statistics_test tests/test_system_statistics.cpp)
  target_link_libraries(server1_system_statistics_test PRIVATE server1_system_statistics uv_a)
  add_executable(server1_system_statistics_backend_test tests/test_system_statistics_backend.cpp src/http/SystemStatistics.cpp)
  target_include_directories(server1_system_statistics_backend_test PRIVATE "${server1_libuv_SOURCE_DIR}/include")
  target_link_libraries(server1_system_statistics_backend_test PRIVATE server1_k00_policy)
  add_test(NAME H01-system-statistics-live COMMAND server1_system_statistics_test)
  add_test(NAME H01-system-statistics-backend COMMAND server1_system_statistics_backend_test)
  if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
    add_test(NAME H01-system-statistics-differential COMMAND "${SERVER1_NODE_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_system_statistics.cjs" "${SERVER1_K12_ORACLE}"
      "$<TARGET_FILE:server1_system_statistics_test>" "$<TARGET_FILE:server1_system_statistics_backend_test>")
  endif()
endif()

add_library(server1_guess_file_index STATIC src/http/GuessFileIndex.cpp)
target_include_directories(server1_guess_file_index PUBLIC include)
target_link_libraries(server1_guess_file_index PUBLIC server1_k00_policy Qt6::Core)
target_link_libraries(colosseum_server_v1 PUBLIC server1_guess_file_index)
target_sources(server1_k11_engine PRIVATE src/policy/SourcePeerAddress.cpp)
if(BUILD_TESTING)
  add_executable(server1_guess_file_index_test tests/test_guess_file_index.cpp)
  target_link_libraries(server1_guess_file_index_test PRIVATE server1_guess_file_index)
  add_test(NAME H01-guess-file-index COMMAND server1_guess_file_index_test --self-test)
  add_executable(server1_k12_source_peer_address_test tests/test_source_peer_address.cpp)
  target_link_libraries(server1_k12_source_peer_address_test PRIVATE server1_k11_engine)
  add_test(NAME K12-source-peer-address COMMAND server1_k12_source_peer_address_test)
  add_test(NAME K12-dns-close-lifecycle COMMAND server1_k10_native_transport_test
    --dns-close "${CMAKE_CURRENT_BINARY_DIR}/k12-dns-close")
  set_tests_properties(K12-dns-close-lifecycle PROPERTIES TIMEOUT 60 ENVIRONMENT "SERVER1_TEST_FIXED_INTERFACE=1")
  if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
    get_filename_component(SERVER1_GUESS_ORACLE_DIR "${SERVER1_K12_ORACLE}" DIRECTORY)
    add_test(NAME H01-guess-file-index-differential COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_guess_file_index.py"
      --candidate "$<TARGET_FILE:server1_guess_file_index_test>" --oracle "${SERVER1_K12_ORACLE}"
      --index "${SERVER1_GUESS_ORACLE_DIR}/../../../MODULE-INDEX.json" --node "${SERVER1_NODE_EXECUTABLE}")
    set_tests_properties(H01-guess-file-index-differential PROPERTIES TIMEOUT 150)
    add_test(NAME K12-source-peer-address-differential COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/server_lab/check_source_peer_address.py"
      --oracle "${SERVER1_K12_ORACLE}" --candidate "$<TARGET_FILE:server1_k12_source_peer_address_test>")
  endif()
endif()
