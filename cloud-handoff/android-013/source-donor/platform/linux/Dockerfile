FROM ubuntu:22.04@sha256:0e0a0fc6d18feda9db1590da249ac93e8d5abfea8f4c3c0c849ce512b5ef8982 AS toolchain

ARG DEBIAN_FRONTEND=noninteractive
ARG TARGETARCH
ARG CMAKE_VERSION=3.31.12
RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        binutils \
        build-essential \
        ca-certificates \
        curl \
        file \
        git \
        ninja-build \
        perl \
        pkg-config \
    && rm -rf /var/lib/apt/lists/* \
    && case "$TARGETARCH" in \
        amd64) cmake_arch=x86_64; cmake_sha=0dc2e9a6860f06bf10bd8fadc03e35d9eeb4df46e33763a7e480e987758f385c ;; \
        arm64) cmake_arch=aarch64; cmake_sha=83f8fd91d2038a56556e1400390fcfe42f79602940c494f6c6f1cdae7f9e7f40 ;; \
        *) exit 2 ;; \
       esac \
    && curl --fail --location --retry 3 --silent --show-error \
        --output /tmp/cmake.tar.gz \
        "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-${cmake_arch}.tar.gz" \
    && echo "$cmake_sha  /tmp/cmake.tar.gz" | sha256sum --check --strict \
    && mkdir -p /opt/cmake \
    && tar -xzf /tmp/cmake.tar.gz --strip-components=1 -C /opt/cmake \
    && rm /tmp/cmake.tar.gz \
    && ln -s /opt/cmake/bin/cmake /usr/local/bin/cmake \
    && ln -s /opt/cmake/bin/ctest /usr/local/bin/ctest \
    && git config --global --add safe.directory '*'

WORKDIR /source

FROM toolchain AS package
ARG TARGETARCH
COPY . /source
RUN --mount=type=cache,id=nuvio-linux-deps-${TARGETARCH},target=/source/platform/linux/.deps \
    --mount=type=cache,id=nuvio-linux-build-${TARGETARCH},target=/source/platform/linux/build \
    case "$TARGETARCH" in \
        amd64) target_architecture=x86_64 ;; \
        arm64) target_architecture=aarch64 ;; \
        *) exit 2 ;; \
    esac \
    && /source/scripts/build-linux-package-in-container.sh "$target_architecture"

FROM scratch AS artifact
COPY --from=package /source/platform/linux/dist/ /
