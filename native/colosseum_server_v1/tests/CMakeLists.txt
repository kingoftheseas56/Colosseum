add_test(NAME P03-02-server1-host COMMAND server1_host)
set_tests_properties(P03-02-server1-host PROPERTIES
  PASS_REGULAR_EXPRESSION "server1 skeleton initialized; streaming-ready=NO"
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/K00.cmake")
server1_register_k00_tests()
