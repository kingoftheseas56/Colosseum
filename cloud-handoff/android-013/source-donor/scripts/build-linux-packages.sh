#!/usr/bin/env bash
set -euo pipefail

script_directory=$(cd "$(dirname "$0")" && pwd -P)
engine_root=$(cd "$script_directory/.." && pwd -P)
dockerfile_root="$engine_root/platform/linux"
distribution_root="$dockerfile_root/dist"

if ! docker info >/dev/null 2>&1; then
    echo "Docker must be running to build Linux packages" >&2
    exit 2
fi

for target_architecture in x86_64 aarch64; do
    case "$target_architecture" in
        x86_64) platform=linux/amd64 ;;
        aarch64) platform=linux/arm64 ;;
    esac
    mkdir -p "$distribution_root"
    docker build \
        --platform "$platform" \
        --file "$dockerfile_root/Dockerfile" \
        --target artifact \
        --output "type=local,dest=$distribution_root" \
        "$engine_root"
done

echo "created Linux x86_64 and aarch64 packages in platform/linux/dist"
