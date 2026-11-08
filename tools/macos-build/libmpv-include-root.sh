#!/bin/bash
# Find the include root for the public <mpv/...> API, not the leaf mpv directory.
# Sourced by the Mac build helper; no compiler or package-manager state changes.
colosseum_libmpv_include_root() {
    local reported=$1
    if [[ -f "$reported/mpv/client.h" && -f "$reported/mpv/render_gl.h" ]]; then
        (cd "$reported" && pwd -P)
        return 0
    fi
    if [[ -f "$reported/client.h" && -f "$reported/render_gl.h" \
          && -f "$reported/../mpv/client.h" && -f "$reported/../mpv/render_gl.h" ]]; then
        (cd "$reported/.." && pwd -P)
        return 0
    fi
    printf 'ERROR: No public mpv/client.h and mpv/render_gl.h under reported include root: %s\n' "$reported" >&2
    return 1
}
