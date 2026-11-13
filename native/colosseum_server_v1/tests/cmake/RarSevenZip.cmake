# Native ports of pinned M1124/M1288; liblzma only decodes archive headers.
find_package(LibLZMA REQUIRED)
target_sources(colosseum_server_v1 PRIVATE src/archive/RarSevenZipSource.cpp)
target_link_libraries(colosseum_server_v1 PRIVATE LibLZMA::LibLZMA)
if(BUILD_TESTING)
  foreach(name rar_sevenzip rar_sevenzip_routes)
    add_executable(server1_${name}_tests tests/test_${name}.cpp)
    target_link_libraries(server1_${name}_tests PRIVATE colosseum_server_v1)
  endforeach()
  if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
    foreach(kind differential routes)
      if(kind STREQUAL "differential")
        set(candidate server1_rar_sevenzip_tests)
      else()
        set(candidate server1_rar_sevenzip_routes_tests)
      endif()
      add_test(NAME A05-rar-sevenzip-${kind} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_rar_sevenzip_tests.py"
        --candidate "$<TARGET_FILE:${candidate}>"
        --oracle "${SERVER1_K12_ORACLE}" --node "${SERVER1_NODE_EXECUTABLE}")
      set_tests_properties(A05-rar-sevenzip-${kind} PROPERTIES TIMEOUT 120)
    endforeach()
  endif()
endif()
