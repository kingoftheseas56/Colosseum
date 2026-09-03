#!/bin/sh
# Assemble the macOS .app bundle for the GPUI shell with cargo-bundle.
#
# Output: target/debug/bundle/osx/Colosseum.app  (gitignored — never commit it).
# Bundle metadata lives in crates/ui-gpui/Cargo.toml under
# [package.metadata.bundle]; the executable is the package's [[bin]] target
# (ui-gpui). No third-party dylibs are bundled: `cargo tree -p ui-gpui`
# resolves to pure-Rust deps plus system frameworks (Metal, AVFoundation),
# which macOS supplies at runtime.
set -eu
cd "$(dirname "$0")/.."

if ! command -v cargo-bundle >/dev/null 2>&1; then
    echo "error: cargo-bundle not on PATH (install: brew install cargo-bundle)" >&2
    exit 1
fi

cargo bundle --package ui-gpui --format osx

APP=target/debug/bundle/osx/Colosseum.app
[ -d "$APP" ] || { echo "error: bundle not produced at $APP" >&2; exit 1; }

echo "bundle ready: $(pwd)/$APP"
