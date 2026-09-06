cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED P03_SOURCE_DIR)
  message(FATAL_ERROR "P03_SOURCE_DIR is required")
endif()

set(required_paths
  "${P03_SOURCE_DIR}/native/colosseum_server_v1/CMakeLists.txt"
  "${P03_SOURCE_DIR}/native/colosseum_server_v1/include/server1/Runtime.h"
  "${P03_SOURCE_DIR}/native/colosseum_server_v1/host/main.cpp"
)

foreach(required_path IN LISTS required_paths)
  if(NOT EXISTS "${required_path}")
    message(FATAL_ERROR "P03 RED: required skeleton path is missing: ${required_path}")
  endif()
endforeach()

message(STATUS "P03 RED: standalone skeleton paths exist")
