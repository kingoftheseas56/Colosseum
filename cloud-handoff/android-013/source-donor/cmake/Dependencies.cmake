include(FetchContent)

function(nuvio_engine_add_libtorrent)
    if(TARGET torrent-rasterbar)
        return()
    endif()

    cmake_policy(PUSH)
    if(POLICY CMP0144)
        cmake_policy(SET CMP0144 NEW)
    endif()
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 OLD)
    endif()
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    if(POLICY CMP0183)
        cmake_policy(SET CMP0183 OLD)
    endif()

    FetchContent_Declare(
        nuvio_boost
        URL https://archives.boost.io/release/1.86.0/source/boost_1_86_0.tar.bz2
        URL_HASH SHA256=1bed88e40401b2cb7a1f76d4bab499e352fa4d0c5f31c0dbae64e24d34d7513b
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_GetProperties(nuvio_boost)
    if(NOT nuvio_boost_POPULATED)
        FetchContent_Populate(nuvio_boost)
    endif()

    set(BOOST_ROOT "${nuvio_boost_SOURCE_DIR}")
    # FindBoost applies toolchain root-path rules while cross-compiling. Point it
    # at the populated, checksummed headers explicitly so Android and Apple
    # builds cannot accidentally fall back to host Boost installations.
    set(Boost_INCLUDE_DIR "${nuvio_boost_SOURCE_DIR}")
    set(Boost_INCLUDE_DIRS "${nuvio_boost_SOURCE_DIR}")
    set(Boost_NO_SYSTEM_PATHS ON)
    set(Boost_NO_BOOST_CMAKE ON)
    set(CMAKE_POLICY_DEFAULT_CMP0144 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0167 OLD)
    set(CMAKE_POLICY_DEFAULT_CMP0183 OLD)

    FetchContent_Declare(
        nuvio_libtorrent
        GIT_REPOSITORY https://github.com/arvidn/libtorrent.git
        GIT_TAG 740a0b9aeabe00e762cc0efe4a0f27593db2550b
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
        # try_signal is part of libtorrent's portable signal handling. The
        # simulation and optional GnuTLS adapter submodules are not used by this
        # build and would multiply every cross-ABI checkout.
        GIT_SUBMODULES deps/try_signal
        GIT_SUBMODULES_RECURSE FALSE
    )
    FetchContent_GetProperties(nuvio_libtorrent)
    if(NOT nuvio_libtorrent_POPULATED)
        FetchContent_Populate(nuvio_libtorrent)
    endif()

    set(
        nuvio_libtorrent_patches
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/libtorrent-2.0.12-macos-route-bounds.patch"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/libtorrent-2.0.12-nuvio-ca-bundle.patch"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/libtorrent-2.0.12-nuvio-apple-trust.patch"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/libtorrent-2.0.12-nuvio-windows-trust.patch"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/libtorrent-2.0.12-nuvio-windows-system-trust.patch"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/libtorrent-2.0.12-nuvio-streaming-stall.patch"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/libtorrent-2.0.12-nuvio-request-queue-limit.patch"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/libtorrent-2.0.12-nuvio-block-start.patch"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/libtorrent-2.0.12-nuvio-connect-boost.patch"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/libtorrent-2.0.12-nuvio-top-priority-first.patch"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/libtorrent-2.0.12-nuvio-metadata-pipeline.patch"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/libtorrent-2.0.12-nuvio-urgent-hedge.patch"
    )
    set(
        nuvio_libtorrent_patch_files
        "src/enum_net.cpp"
        "include/libtorrent/settings_pack.hpp"
        "src/session_impl.cpp"
        "src/session_impl.cpp"
        "src/session_impl.cpp"
        "src/torrent.cpp"
        "src/bt_peer_connection.cpp"
        "src/piece_picker.cpp"
        "src/torrent.cpp"
        "src/piece_picker.cpp"
        "src/ut_metadata.cpp"
        "src/torrent.cpp"
    )
    set(
        nuvio_libtorrent_patch_sentinels
        "message_end = reinterpret_cast<char*>"
        "nuvio_ssl_ca_bundle"
        "nuvio_apple_configure_tls_trust"
        "nuvio_windows_configure_tls_trust"
        "live Windows store without replacing an explicit PEM store"
        "nuvio_stall_budget_ms"
        "nuvio_reqq"
        "nuvio_block_start"
        "nuvio_boost_attempts"
        "nuvio_top"
        "nuvio_metadata_pipeline"
        "nuvio_hedge_blocks"
    )
    # Pin the full source tree before touching it. A source-directory override
    # must not silently substitute a different libtorrent revision.
    execute_process(
        COMMAND git rev-parse HEAD
        WORKING_DIRECTORY "${nuvio_libtorrent_SOURCE_DIR}"
        OUTPUT_VARIABLE nuvio_libtorrent_revision
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE nuvio_libtorrent_revision_result
        ERROR_QUIET
    )
    if(NOT nuvio_libtorrent_revision_result EQUAL 0 OR
       NOT nuvio_libtorrent_revision STREQUAL "740a0b9aeabe00e762cc0efe4a0f27593db2550b")
        message(FATAL_ERROR "libtorrent source is not the pinned 740a0b9 revision")
    endif()

    list(LENGTH nuvio_libtorrent_patches nuvio_libtorrent_patch_count)
    list(LENGTH nuvio_libtorrent_patch_files nuvio_libtorrent_file_count)
    list(LENGTH nuvio_libtorrent_patch_sentinels nuvio_libtorrent_sentinel_count)
    if(NOT nuvio_libtorrent_patch_count EQUAL 12 OR
       NOT nuvio_libtorrent_file_count EQUAL nuvio_libtorrent_patch_count OR
       NOT nuvio_libtorrent_sentinel_count EQUAL nuvio_libtorrent_patch_count)
        message(FATAL_ERROR "Pinned libtorrent patch table must contain twelve complete entries")
    endif()
    math(EXPR nuvio_libtorrent_last_patch "${nuvio_libtorrent_patch_count} - 1")
    foreach(nuvio_libtorrent_patch_index RANGE 0 ${nuvio_libtorrent_last_patch})
        list(GET nuvio_libtorrent_patches ${nuvio_libtorrent_patch_index} nuvio_libtorrent_patch)
        list(GET nuvio_libtorrent_patch_files ${nuvio_libtorrent_patch_index} nuvio_libtorrent_patch_file)
        list(GET nuvio_libtorrent_patch_sentinels ${nuvio_libtorrent_patch_index} nuvio_libtorrent_patch_sentinel)
        # The tracked patches contain LF bytes, but Windows core.autocrlf
        # checks out CRLF. git apply treats CRLF hunk deletions as different
        # source text, so use an LF copy in the build tree on every configure.
        # This normalizes only line endings; divergent hunks must still fail.
        get_filename_component(nuvio_libtorrent_patch_name "${nuvio_libtorrent_patch}" NAME)
        file(READ "${nuvio_libtorrent_patch}" nuvio_libtorrent_patch_text)
        string(REPLACE "\r\n" "\n" nuvio_libtorrent_patch_text "${nuvio_libtorrent_patch_text}")
        string(FIND "${nuvio_libtorrent_patch_text}" "\r" nuvio_libtorrent_bare_cr)
        if(NOT nuvio_libtorrent_bare_cr EQUAL -1)
            message(FATAL_ERROR "Pinned libtorrent patch has invalid line endings: ${nuvio_libtorrent_patch_name}")
        endif()
        set(nuvio_libtorrent_patch_dir "${CMAKE_CURRENT_BINARY_DIR}/nuvio-libtorrent-patches")
        file(MAKE_DIRECTORY "${nuvio_libtorrent_patch_dir}")
        set(nuvio_libtorrent_prepared_patch
            "${nuvio_libtorrent_patch_dir}/${nuvio_libtorrent_patch_name}"
        )
        # CMake file(WRITE) emits host-native CRLF on Windows, so explicitly
        # require UNIX newlines and verify the materialized bytes unchanged.
        string(SHA256 nuvio_libtorrent_expected_sha "${nuvio_libtorrent_patch_text}")
        file(CONFIGURE OUTPUT "${nuvio_libtorrent_prepared_patch}"
             CONTENT "${nuvio_libtorrent_patch_text}" @ONLY NEWLINE_STYLE UNIX)
        file(SHA256 "${nuvio_libtorrent_prepared_patch}" nuvio_libtorrent_actual_sha)
        if(NOT nuvio_libtorrent_actual_sha STREQUAL nuvio_libtorrent_expected_sha)
            message(FATAL_ERROR "Pinned libtorrent patch normalization changed content: ${nuvio_libtorrent_patch_name}")
        endif()
        # A textual sentinel is insufficient evidence: it can survive a
        # partially applied or drifted patch. The full reverse check proves
        # every hunk is already present on repeat configure.
        execute_process(
            COMMAND git apply --unidiff-zero --reverse --check "${nuvio_libtorrent_prepared_patch}"
            WORKING_DIRECTORY "${nuvio_libtorrent_SOURCE_DIR}"
            RESULT_VARIABLE nuvio_libtorrent_already_applied
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(NOT nuvio_libtorrent_already_applied EQUAL 0)
            execute_process(
                COMMAND git apply --unidiff-zero --check "${nuvio_libtorrent_prepared_patch}"
                WORKING_DIRECTORY "${nuvio_libtorrent_SOURCE_DIR}"
                RESULT_VARIABLE nuvio_libtorrent_patch_needed
                OUTPUT_QUIET
                ERROR_QUIET
            )
            if(NOT nuvio_libtorrent_patch_needed EQUAL 0)
                get_filename_component(
                    nuvio_libtorrent_patch_name "${nuvio_libtorrent_patch}" NAME
                )
                message(FATAL_ERROR "Pinned libtorrent patch diverged: ${nuvio_libtorrent_patch_name}")
            endif()
            execute_process(
                COMMAND git apply --unidiff-zero "${nuvio_libtorrent_prepared_patch}"
                WORKING_DIRECTORY "${nuvio_libtorrent_SOURCE_DIR}"
                COMMAND_ERROR_IS_FATAL ANY
            )
        endif()
        file(READ "${nuvio_libtorrent_SOURCE_DIR}/${nuvio_libtorrent_patch_file}"
             nuvio_libtorrent_patch_source)
        string(FIND "${nuvio_libtorrent_patch_source}"
               "${nuvio_libtorrent_patch_sentinel}"
               nuvio_libtorrent_patch_position)
        if(nuvio_libtorrent_patch_position EQUAL -1)
            message(FATAL_ERROR "Pinned libtorrent patch is missing its postcondition: ${nuvio_libtorrent_patch}")
        endif()
    endforeach()

    set(BUILD_SHARED_LIBS OFF)
    set(build_tests OFF)
    set(build_examples OFF)
    set(build_tools OFF)
    set(python-bindings OFF)
    set(developer-options OFF)
    set(logging OFF)
    add_subdirectory(
        "${nuvio_libtorrent_SOURCE_DIR}"
        "${nuvio_libtorrent_BINARY_DIR}"
        EXCLUDE_FROM_ALL
    )
    cmake_policy(POP)
endfunction()
