# Standalone native ECMAScript regex dependency

Upstream: https://bellard.org/quickjs/quickjs-2026-06-04.tar.xz
Archive SHA256: b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a
License: MIT (LICENSE retained, per-file notices retained).

Only libregexp, libunicode, cutils and their headers/tables are built. No
quickjs.c, JS compiler/interpreter, application JS, Node runtime or JS subprocess
is part of production. Regex bytecode is native library implementation detail.

The adapter supplies the allocator, stack guard and native 500ms timeout callback.
It preserves UTF-16 subject code units and CESU-8 vs UTF-8 pattern representation.
Global matching enumerates matches under one deadline, like String.match.
The 64MiB per-operation allocator cap and 512KiB compiler-stack guard are explicit
resource safety bounds, not claims about the oracle's memory exhaustion behavior.
Deadline behavior is qualified for interruption, not identical CPU throughput.

Upstream files are unmodified except cutils.h adds MSVC pack pragmas around the
three already-packed integer structs. MsvcCompat.h is a compiler-only shim for
GNU attributes and count-zero builtins. Linux native build is tested; the locked
Windows kit still needs independent qualification. Upstream file hashes are in
UPSTREAM-SHA256SUMS. The shim and packing patch do not change matching semantics.

Runtime qualification: this backend implements Unicode 17 and modern ECMAScript
regex syntax. The 1,127-case extracted M172/M80 differential passes Node 24.19.0
(Unicode 17). Node 22.16.0 (Unicode 16) disagrees on three explicitly tested
new-letter/property and duplicate-named-group cases. The locked implementation
plan says Node 22 parsing is not qualification of the missing intended
stremio-runtime. Consequently the intended-runtime parity gate remains open;
these modern-runtime results must not be represented as universal JS parity.
