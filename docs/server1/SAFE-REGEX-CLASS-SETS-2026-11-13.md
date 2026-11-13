# Pinned Node 22 native class-set correction

This follow-up resolves the /iv discrepancy preserved in the phase3 SafeFileRegex
checkpoint. The production implementation remains native C/C++; no JavaScript
runtime, parser/interpreter or subprocess is introduced.

## General correction

Pinned V8 12.4 performs Unicode-set algebra on actual case-closed code point
sets. The prior native backend performed it on folded representatives and then
folded the subject at execution. Those representations are not equivalent for
V8's one-character `\q{}` alternatives and special `ASCII` property handling.

The native profile now:

- Builds case closure for ordinary /iv class operands before union, intersection
  and subtraction, and matches the resulting actual ranges case-sensitively.
- Folds one-character `\q{}` alternatives without adding their reverse case
  variants, matching V8's `ParseClassStringDisjunction` behavior. Longer string
  alternatives retain their case-insensitive character matching.
- Preserves V8's literal `ASCII` range and its complement, including Kelvin sign
  and long-s cases. These are handled by `LookupSpecialPropertyValueName` rather
  than ICU's ordinary case-closed property path in the pinned source.
- Uses Unicode's simple-fold representative for the FB05/FB06 ligature pair.
  Either representative worked for closed equivalence classes, but only the
  standard representative matches the one-character `\q{}` behavior.

The case-closure implementation enumerates only entries from the compressed
case-conversion table, not the 1.1 million-code-point Unicode domain. Existing
allocation, stack and match-time limits remain intact. Legacy and /u modes keep
their existing matching representation.

Primary source:
https://github.com/nodejs/node/blob/v22.16.0/deps/v8/src/regexp/regexp-parser.cc

## Verification

All 24,365 matching cases plus 8 file-selection cases pass against the exact
SHA256-locked M80/M172 source under official Node 22.16.0, V8
12.4.254.21-node.26, Unicode 16.0. The default H01 gate now includes everything:

- Original 1,127 comparisons, unchanged.
- 13,985 independently generated Unicode/property/parser inputs.
- Four execution-register allocation regressions.
- The explicit formerly failing /iv expression.
- 2,048 bounded deterministic grammar cases, seed 221600.
- 7,200 systematic class-set comparisons covering union, intersection and
  subtraction; uppercase/lowercase ranges; one/multiple-character strings;
  nested operands; `ASCII` and lowercase properties; word classes; Kelvin sign,
  long s and ligatures; and matching/nonmatching subjects.

The same full corpus passes ASan/UBSan with leak detection disabled because host
ptrace prevents LeakSanitizer's thread inspection. Leak checking is not claimed.
The previous phase3 profile, rebuilt unchanged as a negative control with its
heap fix retained, fails 382 comparisons. No expected answers are relaxed or
replaced; each comparison still executes the pinned source.

`--grammar-fuzz` remains an accepted compatibility argument, but all these cases
now run by default. `--diagnostic-runtime` permits a separate nonqualifying Node
24 comparison. Officially observed Node 24.22.1 / Unicode 17 gives 763
differences in this corpus, as expected for the pinned Node 22 profile. Changing
oracle version is not a substitute for pinned parity.
No Windows qualification, complete ECMAScript conformance or unavailable
intended Stremio-runtime qualification is implied by this bounded suite.
