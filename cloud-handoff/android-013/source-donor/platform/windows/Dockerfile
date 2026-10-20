FROM ubuntu:22.04@sha256:0e0a0fc6d18feda9db1590da249ac93e8d5abfea8f4c3c0c849ce512b5ef8982 AS toolchain

ARG DEBIAN_FRONTEND=noninteractive
ARG CMAKE_VERSION=3.31.12
ARG LLVM_MINGW_VERSION=20260407
ARG LLVM_MINGW_SHA256=a7d68599e828d6bb61e2397bbef43e7a69a5b9c38ee7eec642e05d16b17f3f36
RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        ca-certificates \
        curl \
        git \
        make \
        ninja-build \
        perl \
        python3 \
        unzip \
        xz-utils \
    && rm -rf /var/lib/apt/lists/* \
    && curl --fail --location --retry 3 --silent --show-error \
        --output /tmp/cmake.tar.gz \
        "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-aarch64.tar.gz" \
    && echo "83f8fd91d2038a56556e1400390fcfe42f79602940c494f6c6f1cdae7f9e7f40  /tmp/cmake.tar.gz" | sha256sum --check --strict \
    && mkdir -p /opt/cmake \
    && tar -xzf /tmp/cmake.tar.gz --strip-components=1 -C /opt/cmake \
    && rm /tmp/cmake.tar.gz \
    && ln -s /opt/cmake/bin/cmake /usr/local/bin/cmake \
    && ln -s /opt/cmake/bin/ctest /usr/local/bin/ctest \
    && curl --fail --location --retry 3 --silent --show-error \
        --output /tmp/llvm-mingw.tar.xz \
        "https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VERSION}/llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-aarch64.tar.xz" \
    && echo "${LLVM_MINGW_SHA256}  /tmp/llvm-mingw.tar.xz" | sha256sum --check --strict \
    && mkdir -p /opt/llvm-mingw \
    && tar -xJf /tmp/llvm-mingw.tar.xz --strip-components=1 -C /opt/llvm-mingw \
    && rm /tmp/llvm-mingw.tar.xz \
    && git config --global --add safe.directory '*'

WORKDIR /source

FROM toolchain AS package
RUN apt-get update \
    && apt-get install --yes --no-install-recommends bzip2 \
    && rm -rf /var/lib/apt/lists/*
ARG NUVIO_WINDOWS_ARCH
COPY . /source
RUN --mount=type=cache,id=nuvio-windows-deps,target=/source/platform/windows/.deps \
    --mount=type=cache,id=nuvio-windows-build-${NUVIO_WINDOWS_ARCH},target=/source/platform/windows/build \
    /source/scripts/build-windows-package-in-container.sh "$NUVIO_WINDOWS_ARCH"

FROM scratch AS artifact
COPY --from=package /source/platform/windows/dist/ /
