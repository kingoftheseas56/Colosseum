cmake_minimum_required(VERSION 3.24)
project(colosseum_server_integration_smoke LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/.." colosseum_server_production)

add_executable(colosseum_server_integration_smoke integration_smoke.cpp)
target_link_libraries(colosseum_server_integration_smoke PRIVATE colosseum_server_native)

enable_testing()
add_test(NAME colosseum_server_integration_smoke COMMAND colosseum_server_integration_smoke)
