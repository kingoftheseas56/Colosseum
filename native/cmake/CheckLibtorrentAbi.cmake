# Verify the unexported Windows static-library fallback with the selected headers.
# Package-config builds use the dependency targets exported by their packages.
if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "The Windows libtorrent fallback ABI probe must run on its target platform.")
endif()

set(_colosseum_abi_dir "${CMAKE_CURRENT_BINARY_DIR}/libtorrent-abi")
file(MAKE_DIRECTORY "${_colosseum_abi_dir}")
set(_colosseum_abi_exe "${_colosseum_abi_dir}/probe${CMAKE_EXECUTABLE_SUFFIX}")
set(_colosseum_abi_saved_configuration "${CMAKE_TRY_COMPILE_CONFIGURATION}")
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
try_compile(_colosseum_abi_compiled
    "${_colosseum_abi_dir}/build"
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/../../tests/libtorrent_abi_probe.cpp"
    CMAKE_FLAGS
        "-DCMAKE_BUILD_TYPE:STRING=Release"
        "-DCMAKE_CXX_STANDARD:STRING=17"
        "-DCMAKE_MSVC_RUNTIME_LIBRARY:STRING=MultiThreadedDLL"
        "-DINCLUDE_DIRECTORIES:STRING=${_colosseum_abi_includes}"
    COMPILE_DEFINITIONS -DBOOST_ALL_NO_LIB -DTORRENT_USE_OPENSSL -D_WIN32_WINNT=0x0A00
    LINK_LIBRARIES ${_colosseum_abi_libraries}
    COPY_FILE "${_colosseum_abi_exe}"
    OUTPUT_VARIABLE _colosseum_abi_build_output)
set(CMAKE_TRY_COMPILE_CONFIGURATION "${_colosseum_abi_saved_configuration}")
file(WRITE "${_colosseum_abi_dir}/build.log" "${_colosseum_abi_build_output}")
if(NOT _colosseum_abi_compiled)
    message(FATAL_ERROR "The libtorrent/Boost ABI probe failed to compile. See ${_colosseum_abi_dir}/build.log.")
endif()

execute_process(COMMAND "${_colosseum_abi_exe}"
    TIMEOUT 10
    RESULT_VARIABLE _colosseum_abi_result
    OUTPUT_VARIABLE _colosseum_abi_output
    ERROR_VARIABLE _colosseum_abi_error)
file(WRITE "${_colosseum_abi_dir}/run.log"
    "result=${_colosseum_abi_result}\n${_colosseum_abi_output}${_colosseum_abi_error}")
if(NOT "${_colosseum_abi_result}" STREQUAL "0")
    message(FATAL_ERROR
        "Incompatible libtorrent/Boost dependencies: the session shutdown ABI probe failed (${_colosseum_abi_result}). "
        "Use the same Boost headers that built libtorrent; set BOOST_ROOT and BOOST_INCLUDE_DIR explicitly, "
        "or rebuild libtorrent with your selected Boost. See ${_colosseum_abi_dir}/run.log.")
endif()
message(STATUS "libtorrent/Boost session shutdown ABI probe passed")
