FROM ubuntu:24.04
# Explicit host ABI/display baseline. No Qt, MpvQt, libmpv or build SDK installed.
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 xvfb xauth ca-certificates fonts-dejavu-core \
    libgl1-mesa-dri libegl1 libgl1 dbus-x11 \
    && rm -rf /var/lib/apt/lists/*
# Ubuntu images may already reserve uid 1000; use numeric identity without
# modifying that account. HOME/XDG paths are created by the qualifier.
USER 1000:1000
ENV LIBGL_ALWAYS_SOFTWARE=1
WORKDIR /evidence
