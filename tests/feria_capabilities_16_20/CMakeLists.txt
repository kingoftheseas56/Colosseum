cmake_minimum_required(VERSION 3.24)
project(FeriaCapabilities16To20 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Core Gui Qml Quick QuickTest Test Network Sql WebEngineQuick)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
add_library(capability_owners STATIC
    "${ROOT}/native/SessionStore.h"
    "${ROOT}/native/feria/FeriaAccountStore.cpp"
    "${ROOT}/native/feria/FeriaBrowserPolicy.cpp"
    "${ROOT}/native/feria/app-state/PorticoAppStateBridge.cpp")
target_include_directories(capability_owners PUBLIC "${ROOT}/native" "${ROOT}/native/feria")
target_link_libraries(capability_owners PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Sql)
add_executable(feria_capabilities_runtime main.cpp CapabilitiesHttpFixture.h)
target_compile_definitions(feria_capabilities_runtime PRIVATE FERIA_FIXTURE_ASSET_DIR="${CMAKE_CURRENT_LIST_DIR}/assets")
target_link_libraries(feria_capabilities_runtime PRIVATE capability_owners Qt6::QuickTest Qt6::WebEngineQuick Qt6::Network)
add_executable(feria_download_policy_test "${ROOT}/tests/feria_download_policy_test.cpp")
target_link_libraries(feria_download_policy_test PRIVATE capability_owners)
add_executable(feria_account_store_test "${ROOT}/tests/feria_account_store_test.cpp")
target_link_libraries(feria_account_store_test PRIVATE capability_owners)
enable_testing()
add_test(NAME download_policy COMMAND feria_download_policy_test)
add_test(NAME account_store COMMAND feria_account_store_test)
