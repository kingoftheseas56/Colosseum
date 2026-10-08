cmake_minimum_required(VERSION 3.24)
project(FeriaSourceBindingReproducer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Core Gui Qml Quick QuickTest Network WebEngineQuick)
add_executable(feria_source_binding_reproducer main.cpp)
target_compile_definitions(feria_source_binding_reproducer PRIVATE
    REPRODUCER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(feria_source_binding_reproducer PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickTest Qt6::Network Qt6::WebEngineQuick)
enable_testing()
add_test(NAME source_binding COMMAND feria_source_binding_reproducer)
