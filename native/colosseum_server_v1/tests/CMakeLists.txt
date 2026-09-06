add_test(NAME P03-02-server1-host COMMAND server1_host)
set_tests_properties(P03-02-server1-host PROPERTIES
  PASS_REGULAR_EXPRESSION "server1 skeleton initialized; streaming-ready=NO"
)
