# Native SafeFileRegex pinned-runtime checkpoint

Target: official Node 22.16.0, V8 12.4.254.21-node.26, ICU 77.1, Unicode 16.0.
Source: the SHA256-locked v4.21.1 server bundle, exact extracted M80 and existing
M172 conversion/file-selection oracle. Production remains standalone native C/C++.

## Changes and qualification

- Regenerated every embedded Unicode table from official Unicode 16 data and
  Emoji 16 sequences. Removed all four Unicode 17-only script names using the
  selected UCD alias list, not a hardcoded test-character exception.
- Matched pinned parser restrictions for duplicate named captures, escaped
  capture names, scoped modifiers and the ECMAScript binary-property allowlist.
- Corrected execution-buffer sizing to the native backend's allocation-count
  contract. Repetition registers follow capture slots. Seeded grammar fuzz found
  the old adapter's heap buffer overflow for `/\w{1,2}b/gv`; ASan localized it.
- All original 1,127 matching and 8 nested selection cases remain unchanged.
  Added 13,985 independent Unicode/script/category/property/parser/case-folding
  input cases and four explicit repetition-register regressions: 15,116 exact
  matching comparisons plus 8 selection comparisons pass on pinned Node 22.
- The corpus includes all Unicode 16 simple case-folding pairs, long/short script
  aliases, range boundaries, every Unicode 17-added range, case/property
  complements, group-name escapes and Emoji string properties. Generation and
  hashes are recorded beside the vendored library. No expected output is
  generated from the candidate.
- ASan/UBSan pass the same profile corpus with leak detection disabled. The host
  prevents LeakSanitizer's thread inspection under ptrace; leak checking is not
  claimed. Earlier overflow reproduction is retained in external test evidence.
- Rebuilding the table and fixture produces byte-identical SHA256 values.
- Negative control: the unchanged pre-fix checkpoint fails the original suite
  on exactly the historical three Node 22 mismatches. This confirms that the
  oracle and comparison still detect the errors being corrected.

## Remaining diagnostic mismatch

The separate `--grammar-fuzz` mode runs the 2,048 deterministic generated cases
plus an explicit regression case. It remains failing at this checkpoint:

- Subject: `A `.
- Expression: `/[\q{ab|a}]{1,2}(?<=a)/iv`.
- Node 22 source: valid, false. Native: valid, true.

Pinned V8 folds characters inside `\q{}` but does not add the reverse case
variants to one-character alternatives. Ordinary /iv class operands do receive
case closure. This is a broader class-set representation difference, not a
Unicode-table version mismatch. The diagnostic preserves the observed result
and is not hidden by a changed expected answer or interpreted as a passing gate.
A general correction is separate follow-up work.

Run the normal H01 runner with the official Node 22 executable. Add
`--grammar-fuzz` for the remaining discrepancy, or `--diagnostic-runtime` for a
separate runtime comparison. Node 24.22.1 / Unicode 17 differs by design: the
expanded profile plus grammar diagnostic showed 764 differences in the sampled
inputs, including newer characters/scripts and newly enabled parser syntax.
Node 24 is not substituted for the pinned oracle, and its diagnostic is not a
release gate. The intended Stremio runtime and Windows remain independently
unqualified. No complete server parity or full regex conformance is claimed.

Upstream library, data provenance and reproduction:
`native/colosseum_server_v1/third_party/ecmascript_regex/README.md`.
