cmake_minimum_required(VERSION 3.24)
project(WebEngineStartupPolicyTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Test)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
add_executable(webengine_startup_policy_test
    "${ROOT}/tests/webengine_startup_policy_test.cpp"
    "${ROOT}/native/WebEngineStartupPolicy.cpp")
target_include_directories(webengine_startup_policy_test PRIVATE "${ROOT}/native")
target_link_libraries(webengine_startup_policy_test PRIVATE Qt6::Core Qt6::Test)
enable_testing()
add_test(NAME startup_policy COMMAND webengine_startup_policy_test)
