#!/usr/bin/env bash
set -euo pipefail

script_directory=$(cd "$(dirname "$0")" && pwd -P)
exec "$script_directory/prepare-native-dependencies.sh" "$@"
