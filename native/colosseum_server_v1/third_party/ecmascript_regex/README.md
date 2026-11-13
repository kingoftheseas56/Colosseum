# Standalone native ECMAScript regex dependency

Upstream: https://bellard.org/quickjs/quickjs-2026-06-04.tar.xz
Archive SHA256: b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a
License: MIT (LICENSE retained, per-file notices retained).
Unicode data license: UNICODE-LICENSE.txt (Unicode License V3).

Only libregexp, libunicode, cutils and their headers/tables are built. No
quickjs.c, JS compiler/interpreter, application JS, Node runtime or JS subprocess
is part of production. Regex bytecode is native library implementation detail.

The adapter supplies the allocator, stack guard and native 500ms timeout callback.
It preserves UTF-16 subject code units and CESU-8 vs UTF-8 pattern representation.
Global matching enumerates matches under one deadline, like String.match.
The 64MiB per-operation allocator cap and 512KiB compiler-stack guard are explicit
resource safety bounds, not claims about the oracle's memory exhaustion behavior.
Deadline behavior is qualified for interruption, not identical CPU throughput.
Execution storage uses lre_get_alloc_count(), including repetition registers as
well as visible capture slots. Allocating only 2*capture_count is unsafe.

## Pinned Node 22.16.0 profile

The source qualification target is the official Node 22.16.0 binary, V8
12.4.254.21-node.26, ICU 77.1, Unicode 16.0. This is a native compatibility
profile, not a production Node dependency and not a universal JavaScript claim.

Local changes to upstream:

- libunicode-table.h is fully regenerated from Unicode 16.0.0, including case
  conversion, identifiers, categories, scripts, binary properties and Emoji 16
  sequences. Script aliases absent from the selected UCD are removed before
  generation. libunicode.h reports version 16.0.0.
- libregexp.c rejects duplicate named groups in every alternative/scope and
  scoped modifiers, matching the pinned V8 feature settings. Names are decoded
  by the parser before the uniqueness check, including Unicode escapes.
- libunicode.c excludes the five UCD binary properties that this upstream
  library exposes but V8's ECMAScript allowlist does not: IDS_Unary_Operator,
  Modifier_Combining_Mark, ID_Compat_Math_Start, ID_Compat_Math_Continue and InCB.
  Their aliases are rejected through the same property index, not string hacks.
- /iv set algebra uses actual case-closed ranges, matching V8, and emits those
  ranges without runtime folding. One-character q{} alternatives are folded
  without closure; multi-character alternatives retain case-insensitive
  matching. V8's special ASCII property retains its exact code point range.
  The Unicode case-fold representative for the FB05/FB06 pair is the standard
  simple fold, which matters for one-character q{} alternatives.
- cutils.h retains MSVC pack pragmas around the three already-packed structs.
  MsvcCompat.h remains a compiler-only GNU attribute/count-zero shim.

UPSTREAM-SHA256SUMS identifies the unmodified upstream inputs.
NODE22-PROFILE-SHA256SUMS identifies the four locally profiled vendor files.

V8 parser reference:
https://github.com/nodejs/node/blob/v22.16.0/deps/v8/src/regexp/regexp-parser.cc

## Reproduction

Run `python3 regenerate_unicode16.py --cache /tmp/server1-unicode16` from this
directory. It verifies SHA256 hashes before extracting only the necessary files,
compiles the upstream Unicode generator with a native C compiler, filters script
names against PropertyValueAliases.txt, and regenerates libunicode-table.h.
No JavaScript engine is used. Expected generated table SHA256:
018b5a34eea72adcd55eb8dc021e150604fa7a6bc449be944bd01c2718ac9c57

Pinned source URLs and all input hashes are in regenerate_unicode16.py. Input
archives and build products are not source-controlled. Source of generated test
inputs is tests/generate_safe_file_regex_runtime.py. Given the same cache and
this additional official data file:

https://www.unicode.org/Public/17.0.0/ucd/DerivedAge.txt
SHA256 f8ecdf768bdc210f201abd271d9bc587825618a86a7046a8146cc816393f1998

run the test-input generator with `--ucd /tmp/server1-unicode16/unicode16`,
`--definitions /tmp/server1-unicode16/quickjs/unicode_gen_def.h`, `--age17` pointing
to that file, and `--output` pointing to tests/fixtures/safe-file-regex-runtime.json.
The fixture contains inputs only; every expected answer comes from the exact
pinned source module at test time. Expected fixture SHA256:
e71d71aaab1cd22e7812271c5f8bfa5af0957cbbf482e571e38a84285c3c4472

## Qualification and limits

The H01 runner retains all original 1,127 matching cases and 8 nested selection
cases and requires the pinned runtime fingerprint by default. It runs 24,365
exact matching comparisons: 13,985 data-derived Unicode/parser cases, four
allocation regressions, 2,048 deterministic grammar cases, one explicit /iv q{}
regression, and 7,200 systematic class-set cases, plus the original 1,127.
All are mandatory default coverage. --grammar-fuzz is a compatibility alias;
it no longer changes coverage.

Node 24 comparisons require --diagnostic-runtime; differences are expected
because Unicode tables and parser behavior differ, and do not qualify Node 22.
The phase3 checkpoint's known /iv discrepancy is resolved by the general
class-set correction. That earlier profile fails 382 comparisons in the expanded
corpus, a retained negative control. See
`docs/server1/SAFE-REGEX-CLASS-SETS-2026-11-13.md` for follow-up evidence and
`docs/server1/SAFE-REGEX-NODE22-2026-11-13.md` for the historical checkpoint.

Linux native builds and ASan/UBSan are tested. The locked Windows kit still needs
independent qualification. These checks are not complete ECMAScript conformance
or proof of the unavailable intended stremio-runtime fingerprint.
