cmake_minimum_required(VERSION 3.24)
project(colosseum_server_enginefs_w03 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Core Test)

set(ENGINEFS_SOURCES
    EngineFsControlPlane.cpp
    EngineFsControlPlane.h
)

add_executable(enginefs_control_plane_test
    ${ENGINEFS_SOURCES}
    QtEngineFsTimerScheduler.cpp
    QtEngineFsTimerScheduler.h
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/enginefs/tst_enginefs_control_plane.cpp"
)
target_include_directories(enginefs_control_plane_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(enginefs_control_plane_test PRIVATE Qt6::Core Qt6::Test)
add_test(NAME enginefs_control_plane_test COMMAND enginefs_control_plane_test)

add_executable(enginefs_event_parity_test
    ${ENGINEFS_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/enginefs/tst_enginefs_event_parity.cpp"
)
target_include_directories(enginefs_event_parity_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(enginefs_event_parity_test PRIVATE Qt6::Core Qt6::Test)
add_test(NAME enginefs_event_parity_test COMMAND enginefs_event_parity_test)
