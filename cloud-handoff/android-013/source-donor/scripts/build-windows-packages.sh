#!/usr/bin/env bash
set -euo pipefail

script_directory=$(cd "$(dirname "$0")" && pwd -P)
engine_root=$(cd "$script_directory/.." && pwd -P)
dockerfile_root="$engine_root/platform/windows"
distribution_root="$dockerfile_root/dist"

if ! docker info >/dev/null 2>&1; then
    echo "Docker must be running to build Windows packages" >&2
    exit 2
fi

mkdir -p "$distribution_root"
for target_architecture in x86_64 aarch64; do
    docker build \
        --platform linux/arm64 \
        --build-arg "NUVIO_WINDOWS_ARCH=$target_architecture" \
        --file "$dockerfile_root/Dockerfile" \
        --target artifact \
        --output "type=local,dest=$distribution_root" \
        "$engine_root"
done

echo "created Windows x86_64 and aarch64 packages in platform/windows/dist"
