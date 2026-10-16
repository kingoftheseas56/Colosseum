# native/cmake/android.cmake — Android-only target sources, runtime bundle staging,
# manifest generation, and APK target properties extracted from native/CMakeLists.txt (A02).

target_sources(colosseum PRIVATE
    account/AndroidAccountCredentialStore.cpp
    account/AndroidAccountCredentialStore.h
    account/AndroidAccountSensitiveClipboard.h
    account/AndroidJniSecureStorageBackend.cpp
    account/AndroidJniSecureStorageBackend.h
    account/AndroidSecureStorageBackend.h
    platform/AndroidWindowModeAdapter.cpp
    platform/AndroidWindowModeAdapter.h
    platform/android/AndroidBootstrap.cpp
    platform/android/AndroidBootstrap.h
    engine/VaultAndroidIndexAdapter.cpp
    engine/VaultAndroidIndexAdapter.h
    engine/VaultAndroidStorageBridge.cpp
    engine/VaultAndroidStorageBridge.h
    reader2/AndroidEbookRenderer.cpp
    reader2/AndroidEbookRenderer.h
    player/androidmedia3state.cpp
    player/androidmedia3state.h
    player/androidmedia3item.cpp
    player/androidmedia3item.h
    player/androidmedia3videonode.cpp
    player/androidmedia3videonode.h
    player/androidseekthumbnailer.h
    player/MediaAdmissionProbe_android.cpp
    player/MediaAdmissionProbe.h
    engine/VaultThumbnailer_android.cpp
    player/streamserver_android.cpp
    player/streamserver.h
    player/livestore_android.cpp
    player/livestore.h
    player/powerstore.cpp
    player/powerstore.h
)

# Android needs the same source-shaped runtime tree as desktop, but APKs cannot
# read a checkout beside the native library. Build a generated package overlay
# whose assets/colosseum-runtime tree is filtered for Android and can be copied
# into the app-private filesystem before any source-relative owner is constructed.
set(COLOSSEUM_QML_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../qml")
set(COLOSSEUM_ANDROID_PACKAGE_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/platform/android")
set(COLOSSEUM_ANDROID_PACKAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/android-package")
set(COLOSSEUM_ANDROID_RUNTIME_ROOT
    "${COLOSSEUM_ANDROID_PACKAGE_DIR}/assets/colosseum-runtime")
set(COLOSSEUM_ANDROID_QML_ROOT "${COLOSSEUM_ANDROID_RUNTIME_ROOT}/qml")
file(REMOVE_RECURSE "${COLOSSEUM_ANDROID_PACKAGE_DIR}")
file(MAKE_DIRECTORY "${COLOSSEUM_ANDROID_QML_ROOT}")

file(GLOB_RECURSE COLOSSEUM_ANDROID_PACKAGE_FILES CONFIGURE_DEPENDS
    LIST_DIRECTORIES false RELATIVE "${COLOSSEUM_ANDROID_PACKAGE_SOURCE_ROOT}"
    "${COLOSSEUM_ANDROID_PACKAGE_SOURCE_ROOT}/*")
list(FILTER COLOSSEUM_ANDROID_PACKAGE_FILES EXCLUDE REGEX "^AndroidBootstrap[.](cpp|h)$")
foreach(_package_rel IN LISTS COLOSSEUM_ANDROID_PACKAGE_FILES)
    get_filename_component(_package_out_dir
        "${COLOSSEUM_ANDROID_PACKAGE_DIR}/${_package_rel}" DIRECTORY)
    file(MAKE_DIRECTORY "${_package_out_dir}")
    configure_file("${COLOSSEUM_ANDROID_PACKAGE_SOURCE_ROOT}/${_package_rel}"
                   "${COLOSSEUM_ANDROID_PACKAGE_DIR}/${_package_rel}" COPYONLY)
endforeach()

file(GLOB_RECURSE COLOSSEUM_ANDROID_QML_FILES CONFIGURE_DEPENDS
    LIST_DIRECTORIES false RELATIVE "${COLOSSEUM_QML_SOURCE_ROOT}"
    "${COLOSSEUM_QML_SOURCE_ROOT}/*")
list(FILTER COLOSSEUM_ANDROID_QML_FILES EXCLUDE REGEX "^reader2/Paper[.]qml$")
list(FILTER COLOSSEUM_ANDROID_QML_FILES EXCLUDE REGEX "^player2/")
list(FILTER COLOSSEUM_ANDROID_QML_FILES EXCLUDE REGEX "^player2host/")
# Feria is desktop-only forever: its deck rides QtWebEngine, which Android never ships.
# Main.qml references it only through string-source Loaders gated off on Android.
list(FILTER COLOSSEUM_ANDROID_QML_FILES EXCLUDE REGEX "^feria/")
foreach(_qml_rel IN LISTS COLOSSEUM_ANDROID_QML_FILES)
    get_filename_component(_qml_out_dir "${COLOSSEUM_ANDROID_QML_ROOT}/${_qml_rel}" DIRECTORY)
    file(MAKE_DIRECTORY "${_qml_out_dir}")
    configure_file("${COLOSSEUM_QML_SOURCE_ROOT}/${_qml_rel}"
                   "${COLOSSEUM_ANDROID_QML_ROOT}/${_qml_rel}" COPYONLY)
endforeach()

# Preserve the sibling paths used by QML and native catalogue/extension owners.
foreach(_runtime_dir IN ITEMS assets resources)
    set(_runtime_source_root "${CMAKE_CURRENT_SOURCE_DIR}/../${_runtime_dir}")
    file(GLOB_RECURSE _runtime_files CONFIGURE_DEPENDS
        LIST_DIRECTORIES false RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${_runtime_source_root}/*")
    # Android's AssetManager does not expose dotfiles even when they are present
    # in the source tree. They are development metadata here, never runtime data.
    list(FILTER _runtime_files EXCLUDE REGEX "(^|/)[.][^/]+")
    list(FILTER _runtime_files EXCLUDE REGEX "^resources/reader2/")
    foreach(_runtime_rel IN LISTS _runtime_files)
        get_filename_component(_runtime_out_dir
            "${COLOSSEUM_ANDROID_RUNTIME_ROOT}/${_runtime_rel}" DIRECTORY)
        file(MAKE_DIRECTORY "${_runtime_out_dir}")
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../${_runtime_rel}"
                       "${COLOSSEUM_ANDROID_RUNTIME_ROOT}/${_runtime_rel}" COPYONLY)
    endforeach()
endforeach()

# The WebView serves the common Foliate sources directly through AssetManager.
# Stage one source-of-truth graph; desktop boot and non-EPUB engines stay out.
set(_reader_root "${CMAKE_CURRENT_SOURCE_DIR}/../resources/reader2")
file(GLOB_RECURSE _reader_assets CONFIGURE_DEPENDS LIST_DIRECTORIES false
    RELATIVE "${_reader_root}" "${_reader_root}/vendor/*")
list(FILTER _reader_assets EXCLUDE REGEX "(^|/)[.][^/]+|/pdfjs/|/(pdf|mobi|fb2|comic-book|book)[.]js$")
list(APPEND _reader_assets android_paper.html android_boot.js paper_glue.js)
foreach(_reader_asset IN LISTS _reader_assets)
    get_filename_component(_reader_dir "${COLOSSEUM_ANDROID_PACKAGE_DIR}/assets/reader2/${_reader_asset}" DIRECTORY)
    file(MAKE_DIRECTORY "${_reader_dir}")
    configure_file("${_reader_root}/${_reader_asset}"
                   "${COLOSSEUM_ANDROID_PACKAGE_DIR}/assets/reader2/${_reader_asset}" COPYONLY)
endforeach()

# The staged qmldir must not advertise the desktop Paper type after its file is excluded.
set(_android_reader_qmldir "${COLOSSEUM_ANDROID_QML_ROOT}/reader2/qmldir")
if(EXISTS "${_android_reader_qmldir}")
    file(READ "${_android_reader_qmldir}" _android_reader_qmldir_text)
    string(REPLACE "Paper Paper.qml\r\n" "" _android_reader_qmldir_text "${_android_reader_qmldir_text}")
    string(REPLACE "Paper Paper.qml\n" "" _android_reader_qmldir_text "${_android_reader_qmldir_text}")
    file(WRITE "${_android_reader_qmldir}" "${_android_reader_qmldir_text}")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND}
            "-DQML_ROOT=${COLOSSEUM_ANDROID_QML_ROOT}"
            "-DOUTPUT_FILE=${COLOSSEUM_ANDROID_RUNTIME_ROOT}/qml-build.manifest"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/write_qml_build_manifest.cmake"
    RESULT_VARIABLE _android_qml_manifest_result)
if(NOT _android_qml_manifest_result EQUAL 0)
    message(FATAL_ERROR "Failed to create Android qml-build.manifest")
endif()
execute_process(
    COMMAND ${CMAKE_COMMAND}
            "-DRUNTIME_ROOT=${COLOSSEUM_ANDROID_RUNTIME_ROOT}"
            "-DOUTPUT_FILE=${COLOSSEUM_ANDROID_RUNTIME_ROOT}/runtime-files.manifest"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/write_runtime_bundle_manifest.cmake"
    RESULT_VARIABLE _android_runtime_manifest_result)
if(NOT _android_runtime_manifest_result EQUAL 0)
    message(FATAL_ERROR "Failed to create Android runtime-files.manifest")
endif()

set_property(TARGET colosseum PROPERTY QT_QML_ROOT_PATH "${COLOSSEUM_ANDROID_QML_ROOT}")
set_property(TARGET colosseum PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
    "${COLOSSEUM_ANDROID_PACKAGE_DIR}")
# First-APK contract: one ABI per APK (the configured Qt kit's ABI: arm64-v8a for
# devices, x86_64 for emulators) on Qt 6.11's Android 9/API 28 floor, compiled and
# targeted against the installed API 36 platform.
set_target_properties(colosseum PROPERTIES
    QT_ANDROID_ABIS "${ANDROID_ABI}"
    QT_ANDROID_MIN_SDK_VERSION 28
    QT_ANDROID_COMPILE_SDK_VERSION 36
    QT_ANDROID_TARGET_SDK_VERSION 36
    QT_ANDROID_APP_NAME "Colosseum"
    QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
)
# Qt's TLS plugin dlopens OpenSSL. The static libtorrent dependency alone
# cannot provide these runtime libraries.
set(COLOSSEUM_ANDROID_OPENSSL_LIB_DIR "${OPENSSL_ROOT_DIR}/lib" CACHE PATH
    "Directory containing Android libcrypto_3.so and libssl_3.so")
foreach(_openssl_runtime IN ITEMS libcrypto_3.so libssl_3.so)
    set(_openssl_runtime_path "${COLOSSEUM_ANDROID_OPENSSL_LIB_DIR}/${_openssl_runtime}")
    if(NOT EXISTS "${_openssl_runtime_path}")
        message(FATAL_ERROR "Missing Android TLS runtime: ${_openssl_runtime_path}. Re-run scripts/android/build_deps.sh.")
    endif()
    set_property(TARGET colosseum APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
        "${_openssl_runtime_path}")
endforeach()
