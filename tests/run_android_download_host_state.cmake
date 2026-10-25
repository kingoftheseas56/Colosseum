foreach(required SOURCE_ROOT CLASS_DIR JAVAC JAVA)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${CLASS_DIR}")
execute_process(COMMAND "${JAVAC}" -d "${CLASS_DIR}"
    "${SOURCE_ROOT}/native/platform/android/src/org/colosseum/platform/BackgroundDownloadHostState.java"
    "${SOURCE_ROOT}/tests/android/BackgroundDownloadHostStateTest.java"
    RESULT_VARIABLE compile_result)
if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR "Android download-host policy compilation failed: ${compile_result}")
endif()
execute_process(COMMAND "${JAVA}" -cp "${CLASS_DIR}"
    org.colosseum.platform.BackgroundDownloadHostStateTest RESULT_VARIABLE test_result)
if(NOT test_result EQUAL 0)
    message(FATAL_ERROR "Android download-host policy test failed: ${test_result}")
endif()
