add_test(NAME P03-02-server1-host COMMAND server1_host)
set_tests_properties(P03-02-server1-host PROPERTIES
  PASS_REGULAR_EXPRESSION "server1 skeleton initialized; streaming-ready=NO"
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/K00.cmake")
server1_register_k00_tests()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/K01.cmake")
server1_register_k01_tests()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/K13.cmake")
server1_register_k13_tests()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/H00.cmake")
server1_register_h00_tests()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/M00.cmake")
server1_register_m00_tests()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/K02.cmake")
server1_register_k02_tests()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/K05.cmake")
server1_register_k05_tests()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/K06.cmake")
server1_register_k06_tests()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/K03.cmake")
server1_register_k03_tests()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/K07.cmake")
server1_register_k07_tests()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/K09.cmake")
server1_register_k09_tests()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/K04.cmake")
server1_register_k04_tests()

add_executable(server1_p08_torrent_transport_contract_test
  "${CMAKE_CURRENT_SOURCE_DIR}/test_torrent_transport_contract.cpp")
target_link_libraries(server1_p08_torrent_transport_contract_test PRIVATE
  server1_p08_torrent_transport_contract)
target_compile_features(server1_p08_torrent_transport_contract_test PRIVATE cxx_std_17)
target_compile_options(server1_p08_torrent_transport_contract_test PRIVATE
  "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
add_test(NAME P08-T-torrent-transport-contract
  COMMAND server1_p08_torrent_transport_contract_test)
set_tests_properties(P08-T-torrent-transport-contract PROPERTIES
  LABELS "server1;unit;P08-T")

add_executable(server1_spine_contracts_link_test test_spine_contracts.cpp)
target_link_libraries(server1_spine_contracts_link_test PRIVATE
  server1_k02_piece_buffer
  server1_k03_scheduler_selections
  server1_k04_scheduler_actions
  server1_k05_swarm_metadata
  server1_k06_persistent_store
  server1_k07_circular_store
  server1_k09_peer_search
  server1_p08_torrent_transport_contract)
target_compile_features(server1_spine_contracts_link_test PRIVATE cxx_std_17)
target_compile_options(server1_spine_contracts_link_test PRIVATE
  "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
add_test(NAME INT-W3-spine-contracts-link COMMAND server1_spine_contracts_link_test)
set_tests_properties(INT-W3-spine-contracts-link PROPERTIES
  LABELS "server1;unit;INT-W3"
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
