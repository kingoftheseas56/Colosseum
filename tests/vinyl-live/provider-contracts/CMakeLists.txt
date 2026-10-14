cmake_minimum_required(VERSION 3.21)
project(vinyl_provider_resilience_contracts LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Test)
add_executable(vinyl_provider_resilience_contracts
    ../provider_contracts.cpp
    ../../../native/vinyl/VinylProvider.h)
target_include_directories(vinyl_provider_resilience_contracts PRIVATE ../../../native)
target_link_libraries(vinyl_provider_resilience_contracts PRIVATE Qt6::Core Qt6::Test)
target_compile_definitions(vinyl_provider_resilience_contracts PRIVATE
    VINYL_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}/../../..\")
enable_testing()
add_test(NAME vinyl_provider_resilience_contracts COMMAND vinyl_provider_resilience_contracts)
