# Downloader and ZIP/TAR/GZIP archive services. RAR/7z/FTP/NNTP are separate.
find_package(ZLIB REQUIRED)
target_sources(colosseum_server_v1 PRIVATE
  src/downloads/DownloadManager.cpp src/http/DownloadRoutes.cpp
  src/archive/ArchiveKeys.cpp src/archive/MultipartSource.cpp
  src/archive/RemoteByteSource.cpp src/archive/ZipSource.cpp
  src/archive/ZipEntryReader.cpp src/archive/ArchiveByteSource.cpp
  src/http/ArchiveRoutes.cpp src/archive/TarSource.cpp src/archive/GzipSource.cpp)
target_link_libraries(colosseum_server_v1 PRIVATE ZLIB::ZLIB)

if(BUILD_TESTING)
  foreach(name download_manager archive_sources archive_remote zip zip_stream zip_lifecycle archive_routes tar_gzip)
    add_executable(server1_${name}_tests tests/test_${name}.cpp)
    target_link_libraries(server1_${name}_tests PRIVATE colosseum_server_v1)
  endforeach()
  add_test(NAME N07-download-lifecycle COMMAND server1_download_manager_tests)
  add_test(NAME A00-remote-byte-source COMMAND server1_archive_remote_tests)
  add_test(NAME A02-zip-lifecycle COMMAND server1_zip_lifecycle_tests)
  set_tests_properties(N07-download-lifecycle A00-remote-byte-source A02-zip-lifecycle
    PROPERTIES TIMEOUT 30)
  if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
    add_test(NAME A04-tar-gzip-differential COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tar_gzip_tests.py"
      --candidate "$<TARGET_FILE:server1_tar_gzip_tests>"
      --oracle "${SERVER1_K12_ORACLE}")
    set_tests_properties(A04-tar-gzip-differential PROPERTIES TIMEOUT 60)
  endif()
  foreach(kind zip stream routes)
    if(kind STREQUAL "zip")
      set(candidate server1_zip_tests)
    elseif(kind STREQUAL "stream")
      set(candidate server1_zip_stream_tests)
    else()
      set(candidate server1_archive_routes_tests)
    endif()
    add_test(NAME A02-${kind}-native COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_archive_tests.py"
      --candidate "$<TARGET_FILE:${candidate}>" --kind "${kind}")
    set_tests_properties(A02-${kind}-native PROPERTIES TIMEOUT 45)
  endforeach()
  if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
    add_test(NAME N07-download-differential COMMAND "${SERVER1_NODE_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_download_oracle.cjs"
      "${SERVER1_K12_ORACLE}" "$<TARGET_FILE:server1_download_manager_tests>")
    add_test(NAME A00-source-differential COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_archive_tests.py"
      --candidate "$<TARGET_FILE:server1_archive_sources_tests>" --kind sources
      --oracle "${SERVER1_K12_ORACLE}" --node "${SERVER1_NODE_EXECUTABLE}")
    add_test(NAME A02-zip-differential COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_archive_tests.py"
      --candidate "$<TARGET_FILE:server1_zip_tests>" --kind zip
      --oracle "${SERVER1_K12_ORACLE}" --node "${SERVER1_NODE_EXECUTABLE}")
    set_tests_properties(N07-download-differential A00-source-differential
      A02-zip-differential PROPERTIES TIMEOUT 90)
  endif()
endif()
