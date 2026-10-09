cmake_minimum_required(VERSION 3.21)
project(feria_native_player_runtime LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.11 REQUIRED COMPONENTS Quick WebEngineQuick WebChannel Network Sql Test)
find_package(MpvQt REQUIRED)
set(PLAYER_SOURCES ../../native/player/mpvitem.cpp ../../native/player/mpvitem.h
    ../../native/feria/FeriaNativePlayer.cpp ../../native/feria/FeriaNativePlayer.h
    ../../native/feria/FeriaYouTubeSource.cpp ../../native/feria/FeriaYouTubeSource.h)
qt_add_executable(feria_native_player_runtime main.cpp ${PLAYER_SOURCES}
    ../../native/feria/FeriaBrowserPolicy.cpp ../../native/feria/FeriaBrowserPolicy.h)
target_link_libraries(feria_native_player_runtime PRIVATE Qt6::Quick Qt6::WebEngineQuick Qt6::WebChannel Qt6::Sql Qt6::Network Qt6::Test MpvQt::MpvQt)
qt_add_executable(feria_native_player_contract tst_contract.cpp ${PLAYER_SOURCES})
target_link_libraries(feria_native_player_contract PRIVATE Qt6::Quick Qt6::Test MpvQt::MpvQt)
enable_testing()
add_test(NAME native_player_contract COMMAND feria_native_player_contract)
