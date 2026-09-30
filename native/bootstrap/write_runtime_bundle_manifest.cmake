if(NOT DEFINED RUNTIME_ROOT OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "RUNTIME_ROOT and OUTPUT_FILE are required")
endif()
if(NOT EXISTS "${RUNTIME_ROOT}/qml/Main.qml")
    message(FATAL_ERROR "Runtime root has no qml/Main.qml: ${RUNTIME_ROOT}")
endif()
if(NOT EXISTS "${RUNTIME_ROOT}/qml-build.manifest")
    message(FATAL_ERROR "Runtime root has no qml-build.manifest: ${RUNTIME_ROOT}")
endif()

file(GLOB_RECURSE runtime_files
    LIST_DIRECTORIES false
    RELATIVE "${RUNTIME_ROOT}"
    "${RUNTIME_ROOT}/*")
list(FILTER runtime_files EXCLUDE REGEX "^runtime-files[.]manifest$")
list(SORT runtime_files)
if(NOT runtime_files)
    message(FATAL_ERROR "Runtime root is empty: ${RUNTIME_ROOT}")
endif()

set(material "")
set(entries "")
foreach(relative IN LISTS runtime_files)
    string(REPLACE "\\" "/" normalized "${relative}")
    file(SHA256 "${RUNTIME_ROOT}/${relative}" file_hash)
    string(TOLOWER "${file_hash}" file_hash)
    string(APPEND material "${normalized}\n${file_hash}\n")
    string(APPEND entries "file=${normalized}\t${file_hash}\n")
endforeach()
string(SHA256 bundle_hash "${material}")
string(TOLOWER "${bundle_hash}" bundle_hash)
file(WRITE "${OUTPUT_FILE}"
    "schema=1\nbundleSha256=${bundle_hash}\n${entries}")
