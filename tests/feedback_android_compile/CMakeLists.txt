cmake_minimum_required(VERSION 3.22)
project(FeedbackAndroidCompile LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Gui)
add_library(feedback_android_compile STATIC
    ../../native/feedback/AndroidFeedbackBrowser.cpp
    ../../native/feedback/AndroidFeedbackBrowser.h)
target_link_libraries(feedback_android_compile PRIVATE Qt6::Gui)
