cmake_minimum_required(VERSION 3.21)
project(CollectionsLinksChecks LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Test)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(collection_share_link_test collection_share_link_test.cpp ${ROOT}/native/account/CollectionShareLink.cpp)
target_include_directories(collection_share_link_test PRIVATE ${ROOT}/native)
target_link_libraries(collection_share_link_test PRIVATE Qt6::Core Qt6::Test)
enable_testing()
add_test(NAME collections.links COMMAND collection_share_link_test)

add_executable(collection_link_activation_test collection_link_activation_test.cpp
    ${ROOT}/native/bootstrap/CollectionLinkActivation.cpp ${ROOT}/native/bootstrap/CollectionLinkActivation.h
    ${ROOT}/native/bootstrap/InstanceLock.cpp ${ROOT}/native/bootstrap/StartupLayout.cpp
    ${ROOT}/native/account/CollectionShareLink.cpp ${ROOT}/native/account/AccountServiceEndpoint.cpp)
target_include_directories(collection_link_activation_test PRIVATE ${ROOT}/native)
target_link_libraries(collection_link_activation_test PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test)
add_test(NAME collections.activation COMMAND collection_link_activation_test)
set_tests_properties(collections.activation PROPERTIES TIMEOUT 30)
