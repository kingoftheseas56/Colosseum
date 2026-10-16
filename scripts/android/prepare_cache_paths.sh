#!/usr/bin/env bash
# actions/cache rejects '.' and '..' path components, even in absolute paths.
# Keep the existing sibling directories and publish canonical paths for every
# subsequent installer, restore and save step. No directories are moved/created.
set -euo pipefail

# Match install_toolchain.sh: hosted runners provide their preinstalled SDK.
ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-${COLOSSEUM_ANDROID_TOOLCHAIN:-}/android-sdk}"

for name in COLOSSEUM_ANDROID_TOOLCHAIN COLOSSEUM_ANDROID_DEPS GITHUB_ENV GITHUB_OUTPUT ANDROID_SDK_ROOT; do
    value="${!name:-}"
    if [[ -z "$value" || "$value" != /* || "$value" == *$'\n'* || "$value" == *$'\r'* ]]; then
        printf 'Invalid %s: a nonempty absolute single-line path is required\n' "$name" >&2
        exit 1
    fi
done

toolchain="$(realpath -m -- "$COLOSSEUM_ANDROID_TOOLCHAIN")"
deps="$(realpath -m -- "$COLOSSEUM_ANDROID_DEPS")"
sdk="$(realpath -m -- "$ANDROID_SDK_ROOT")"
if [[ "$toolchain" == / || "$deps" == / || "$sdk" == / ]]; then
    printf 'Android cache prefixes must not resolve to the filesystem root\n' >&2
    exit 1
fi

# Write only after validating all prefixes, so failure cannot leave partial
# overrides. Scripts consume GITHUB_ENV; cache actions use explicit outputs.
printf 'COLOSSEUM_ANDROID_TOOLCHAIN=%s\nCOLOSSEUM_ANDROID_DEPS=%s\nANDROID_SDK_ROOT=%s\n' \
    "$toolchain" "$deps" "$sdk" >> "$GITHUB_ENV"
printf 'toolchain=%s\ndeps=%s\nsdk=%s\n' "$toolchain" "$deps" "$sdk" >> "$GITHUB_OUTPUT"
printf 'Android toolchain cache: %s\nAndroid dependency cache: %s\nAndroid SDK: %s\n' "$toolchain" "$deps" "$sdk"
