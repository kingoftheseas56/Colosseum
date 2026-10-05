# Optional standalone runner, not required by the normal colosseum.qml gate.
cmake_minimum_required(VERSION 3.21)
project(Arc64M1Visual LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Qml QuickTest Test)
add_executable(arc64_m1_visual_runner ../arc64_m1_visual_runner.cpp)
target_link_libraries(arc64_m1_visual_runner PRIVATE Qt6::Core Qt6::Qml Qt6::QuickTest Qt6::Test)
