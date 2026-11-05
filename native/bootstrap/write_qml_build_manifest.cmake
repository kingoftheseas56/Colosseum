if(NOT DEFINED QML_ROOT OR QML_ROOT STREQUAL "")
    message(FATAL_ERROR "QML_ROOT is required")
endif()
if(NOT DEFINED OUTPUT_FILE OR OUTPUT_FILE STREQUAL "")
    message(FATAL_ERROR "OUTPUT_FILE is required")
endif()
if(NOT EXISTS "${QML_ROOT}/Main.qml")
    message(FATAL_ERROR "QML root has no Main.qml: ${QML_ROOT}")
endif()

file(GLOB_RECURSE qml_files
    LIST_DIRECTORIES false
    RELATIVE "${QML_ROOT}"
    "${QML_ROOT}/*")
list(SORT qml_files)
if(NOT qml_files)
    message(FATAL_ERROR "QML root is empty: ${QML_ROOT}")
endif()

set(material "")
foreach(relative IN LISTS qml_files)
    string(REPLACE "\\" "/" normalized "${relative}")
    file(SHA256 "${QML_ROOT}/${relative}" file_hash)
    string(APPEND material "${normalized}\n${file_hash}\n")
endforeach()

string(SHA256 qml_tree_sha256 "${material}")
set(manifest_contents "schema=1\nqmlTreeSha256=${qml_tree_sha256}\n")

# Fast source launches verify the build-aligned QML tree without refreshing the
# manifest. Only a successful native build may update that manifest.
if(VERIFY_ONLY)
    if(NOT EXISTS "${OUTPUT_FILE}")
        message(FATAL_ERROR "QML build manifest missing. Run launch.bat --build.")
    endif()
    file(READ "${OUTPUT_FILE}" built_manifest_contents)
    if(NOT built_manifest_contents STREQUAL manifest_contents)
        message(FATAL_ERROR "QML source differs from the last build. Run launch.bat --build.")
    endif()
    message(STATUS "QML build manifest matches the current source tree")
    return()
endif()

get_filename_component(output_dir "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
file(WRITE "${OUTPUT_FILE}" "${manifest_contents}")
message(STATUS "QML build manifest: ${qml_tree_sha256}")
