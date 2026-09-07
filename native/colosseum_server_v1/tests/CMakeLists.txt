add_executable(server1_runtime_lifecycle_test
    runtime_lifecycle.cpp
)
target_link_libraries(server1_runtime_lifecycle_test PRIVATE server1::runtime Qt6::Core)
add_test(NAME server1.runtime.lifecycle COMMAND server1_runtime_lifecycle_test)
