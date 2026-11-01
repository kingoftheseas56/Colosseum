# Opt-in Android torrent playback backend (Nuvio engine candidate).
#
# OFF (default) preserves the existing unavailable Android StreamServer stub;
# Windows and desktop Stream behavior are untouched either way. Enabling is
# fail-closed: without a prepared, hardened, stamp-verified private engine
# artifact the configure fails loudly — the unpatched release AAR must never
# silently serve as the hardened backend.
option(
    COLOSSEUM_ANDROID_NUVIO_BACKEND
    "Enable the opt-in Android Nuvio torrent playback backend (requires a prepared hardened engine prefix)"
    OFF
)

if(COLOSSEUM_ANDROID_NUVIO_BACKEND)
    if(NOT ANDROID)
        message(
            FATAL_ERROR
            "COLOSSEUM_ANDROID_NUVIO_BACKEND is Android-only; configure the Android target or leave it OFF."
        )
    endif()
    set(
        COLOSSEUM_NUVIO_ENGINE_PREFIX
        ""
        CACHE
        PATH
        "Prepared hardened Nuvio engine prefix (see scripts/android/prepare_nuvio_engine.py)"
    )
    if(NOT COLOSSEUM_NUVIO_ENGINE_PREFIX)
        message(
            FATAL_ERROR
            "COLOSSEUM_ANDROID_NUVIO_BACKEND=ON requires COLOSSEUM_NUVIO_ENGINE_PREFIX: prepare the hardened engine with scripts/android/prepare_nuvio_engine.py. An unpatched release AAR is never acceptable."
        )
    endif()
    # arm64-v8a is the product ABI. x86_64 exists only as an explicit,
    # separately labelled emulator diagnostic; it is validated with its own
    # ELF identity and never stands in for the ARM64 artifact.
    option(
        COLOSSEUM_ANDROID_NUVIO_X86_64_DIAGNOSTIC
        "Allow the hardened Nuvio backend in an x86_64 emulator diagnostic build"
        OFF
    )
    if(ANDROID_ABI STREQUAL "arm64-v8a")
        set(COLOSSEUM_NUVIO_ENGINE_ABI "arm64-v8a")
    elseif(ANDROID_ABI STREQUAL "x86_64" AND COLOSSEUM_ANDROID_NUVIO_X86_64_DIAGNOSTIC)
        set(COLOSSEUM_NUVIO_ENGINE_ABI "x86_64")
    else()
        message(
            FATAL_ERROR
            "COLOSSEUM_ANDROID_NUVIO_BACKEND supports arm64-v8a; ANDROID_ABI=${ANDROID_ABI} requires COLOSSEUM_ANDROID_NUVIO_X86_64_DIAGNOSTIC=ON (x86_64 only)."
        )
    endif()
    set(
        COLOSSEUM_NUVIO_ENGINE_LIBRARY
        "${COLOSSEUM_NUVIO_ENGINE_PREFIX}/android/${COLOSSEUM_NUVIO_ENGINE_ABI}/lib/libnuvio_engine.so"
    )
    set(
        COLOSSEUM_NUVIO_ENGINE_STAMP
        "${COLOSSEUM_NUVIO_ENGINE_PREFIX}/.colosseum-nuvio-hardened-stamp"
    )
    if(NOT EXISTS "${COLOSSEUM_NUVIO_ENGINE_LIBRARY}")
        message(
            FATAL_ERROR
            "Hardened Nuvio engine library missing: ${COLOSSEUM_NUVIO_ENGINE_LIBRARY}. Run scripts/android/prepare_nuvio_engine.py."
        )
    endif()
    # The gate validates the CONTENTS of the stamp and the artifact it
    # describes (pinned source commit, vendored patch identity, library
    # SHA-256, arm64 ELF identity) — stamp existence alone proves nothing.
    find_package(Python3 COMPONENTS Interpreter)
    if(NOT Python3_Interpreter_FOUND)
        message(
            FATAL_ERROR
            "Validating the hardened Nuvio engine artifact requires Python 3."
        )
    endif()
    execute_process(
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/../../scripts/android/verify_nuvio_hardened_artifact.py"
            --prefix "${COLOSSEUM_NUVIO_ENGINE_PREFIX}"
            --abi "${COLOSSEUM_NUVIO_ENGINE_ABI}"
        RESULT_VARIABLE _nuvio_artifact_verification
        OUTPUT_VARIABLE _nuvio_artifact_verification_out
        ERROR_VARIABLE _nuvio_artifact_verification_err
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _nuvio_artifact_verification EQUAL 0)
        message(
            FATAL_ERROR
            "Hardened Nuvio engine artifact failed content validation (${COLOSSEUM_NUVIO_ENGINE_STAMP}): ${_nuvio_artifact_verification_err}. Never use the unpatched release AAR as the hardened backend."
        )
    endif()
    message(STATUS "${_nuvio_artifact_verification_out}")
endif()
