# Android package identity/version input validation (PF-10).
#
# Validates the configurable Android identity cache inputs before they reach Qt's
# androiddeployqt/Gradle manifest, so a malformed or accidental value fails at configure
# time instead of producing a separate app or an un-updatable package. Accepted lanes:
#   - default (empty id): the template package from AndroidManifest.xml;
#   - the permanent id world.getcolosseum.app and its side-by-side lanes
#     world.getcolosseum.app.<lane> (the authorized Beta is world.getcolosseum.app.beta);
#   - Qt's development namespace org.qtproject.example[.colosseum[.<id>]].
# versionCode must be a positive decimal integer no larger than 2100000000 (Android/Play
# upper bound); versionName and the launcher label must be single-line text without XML-special characters.
# Pure CMake (no Android toolchain needed) so tests can exercise it in script mode.

function(colosseum_validate_android_identity application_id app_name version_name version_code)
    if(NOT "${application_id}" STREQUAL "")
        if(NOT application_id MATCHES "^[a-z][a-z0-9_]*([.][a-z][a-z0-9_]*)+$")
            message(FATAL_ERROR
                "COLOSSEUM_ANDROID_APPLICATION_ID '${application_id}' is not a valid lowercase Android package id")
        endif()
        if(NOT application_id MATCHES "^world[.]getcolosseum[.]app([.][a-z][a-z0-9_]*)?$"
                AND NOT application_id MATCHES "^org[.]qtproject[.]example([.]colosseum([.][a-z][a-z0-9_]*)?)?$")
            message(FATAL_ERROR
                "COLOSSEUM_ANDROID_APPLICATION_ID '${application_id}' is outside the Colosseum lanes "
                "(world.getcolosseum.app, world.getcolosseum.app.<lane> such as .beta, or org.qtproject.example.colosseum[.<id>])")
        endif()
    endif()
    if("${app_name}" STREQUAL "" OR app_name MATCHES "[\r\n\t]" OR app_name MATCHES "[\"<>&]")
        message(FATAL_ERROR "COLOSSEUM_ANDROID_APP_NAME must be a non-empty single-line label without \" < > &")
    endif()
    if("${version_name}" STREQUAL "" OR NOT version_name MATCHES "^[0-9A-Za-z]"
            OR version_name MATCHES "[\r\n\t]" OR version_name MATCHES "[\"<>&]")
        message(FATAL_ERROR
            "COLOSSEUM_ANDROID_VERSION_NAME '${version_name}' must be a non-empty single line starting alphanumeric, without \" < > &")
    endif()
    if(NOT "${version_code}" STREQUAL "")
        if(NOT version_code MATCHES "^[1-9][0-9]*$")
            message(FATAL_ERROR
                "COLOSSEUM_ANDROID_VERSION_CODE '${version_code}' must be a positive decimal integer")
        endif()
        string(LENGTH "${version_code}" _colosseum_version_code_length)
        if(_colosseum_version_code_length GREATER 10 OR version_code GREATER 2100000000)
            message(FATAL_ERROR
                "COLOSSEUM_ANDROID_VERSION_CODE '${version_code}' exceeds the Android maximum 2100000000")
        endif()
    endif()
endfunction()
