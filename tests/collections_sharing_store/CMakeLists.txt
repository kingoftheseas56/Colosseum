cmake_minimum_required(VERSION 3.21)
project(CollectionsSharingStoreChecks LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui)
add_executable(collections_sharing_store_test
    collections_sharing_store_test.cpp
    ../../native/NamedCollectionsStore.cpp
    ../../native/NamedCollectionsStore.h)
target_include_directories(collections_sharing_store_test PRIVATE ../../native)
target_link_libraries(collections_sharing_store_test PRIVATE Qt6::Core Qt6::Gui)
if(MSVC)
    target_compile_options(collections_sharing_store_test PRIVATE /utf-8)
endif()
enable_testing()
add_test(NAME colosseum.collections_sharing_store COMMAND collections_sharing_store_test)
