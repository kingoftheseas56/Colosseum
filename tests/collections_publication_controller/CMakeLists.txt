cmake_minimum_required(VERSION 3.21)
project(CollectionsPublicationControllerChecks LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Test)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(collection_publication_controller_test
    controller_test.cpp
    ${ROOT}/native/NamedCollectionsStore.cpp
    ${ROOT}/native/account/AccountClient.cpp
    ${ROOT}/native/account/SyncProtocol.cpp
    ${ROOT}/native/account/AccountHttpTransport.cpp
    ${ROOT}/native/account/AccountTransport.h
    ${ROOT}/native/account/CollectionPublicationController.cpp)
target_include_directories(collection_publication_controller_test PRIVATE ${ROOT}/native)
target_link_libraries(collection_publication_controller_test PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test)
enable_testing()
add_test(NAME collections.publication_controller COMMAND collection_publication_controller_test)
set_tests_properties(collections.publication_controller PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
