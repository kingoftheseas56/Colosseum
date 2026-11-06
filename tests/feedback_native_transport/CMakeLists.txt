cmake_minimum_required(VERSION 3.21)
project(colosseum_feedback_native_transport LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Network Test)
set(CMAKE_AUTOMOC ON)
enable_testing()

add_executable(tst_feedback_native_transport
    tst_feedback_native_transport.cpp
    ../../native/feedback/FeedbackRequestService.cpp
    ../../native/feedback/FeedbackRequestService.h
)
target_compile_features(tst_feedback_native_transport PRIVATE cxx_std_17)
target_link_libraries(tst_feedback_native_transport PRIVATE Qt6::Network Qt6::Test)
add_test(NAME feedback_native_transport COMMAND tst_feedback_native_transport)
