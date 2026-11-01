#!/bin/sh
set -eu
# Run inside a private D-Bus session and temporary keyring. This never unlocks
# or reads the user's desktop keyring. Supply the configured build directory.
build_dir=$(realpath "$1")
test_dir=$(mktemp -d /tmp/colosseum-account-keyring.XXXXXX)
trap 'case "$test_dir" in /tmp/colosseum-account-keyring.*) rm -rf -- "$test_dir";; esac' EXIT
export XDG_DATA_HOME="$test_dir/data"
export XDG_CONFIG_HOME="$test_dir/config"
export XDG_CACHE_HOME="$test_dir/cache"
export XDG_RUNTIME_DIR="$test_dir/runtime"
export GNOME_KEYRING_CONTROL="$test_dir/control"
mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
export COLOSSEUM_ACCOUNT_TEST_BUILD="$build_dir"
dbus-run-session -- sh -eu -c '
    printf "colosseum-fixture-password" | gnome-keyring-daemon --unlock --components=secrets --control-directory "$GNOME_KEYRING_CONTROL"
    ctest --test-dir "$COLOSSEUM_ACCOUNT_TEST_BUILD" --output-on-failure
    export COLOSSEUM_APPDATA_TAG=account-storage-test-locked
    "$COLOSSEUM_ACCOUNT_TEST_BUILD/tst_account_secure_storage" --seed
    "$COLOSSEUM_ACCOUNT_TEST_BUILD/tst_account_secure_storage" --locked-delete
'
