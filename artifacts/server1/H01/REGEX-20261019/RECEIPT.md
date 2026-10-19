# H01 bounded native file filters and bare-hash selection

Native implementation: standalone MIT libregexp/libunicode/cutils C code, without
QuickJS interpreter/compiler, Node, application JavaScript or subprocess in production.
Upstream provenance and compiler adaptation are recorded beside the vendored sources.

Exact SHA-locked M80/M172 differential: 1,127 regex cases and 8 nested file-selector
cases pass under Node 24.19.0 / Unicode 17. Includes flags, backreferences, lookaround,
Unicode sets, legacy escapes, lone surrogates, global enumeration and 500ms interruption.
ASan/UBSan passes the same corpus (LeakSanitizer disabled under sandbox ptrace).
The nested selector deliberately retains source's empty-string Array.find quirk.
Real torrent HTTP fixture now covers bare hash, query f overriding invalid index,
and create fileMustInclude returning guessedFileIdx.

Explicit open gates:
- Intended stremio-runtime remains unfingerprinted. Node22/Unicode16 disagrees on
  three added modern-property/duplicate-name cases. Do not claim runtime-independent
  ECMAScript compatibility; see vendored README.
- 64 MiB regex allocator and 512 KiB compiler-stack caps are safety bounds, not
  oracle memory-exhaustion parity. 500ms deadline interruption is tested, not equal
  CPU throughput. Compiler work is stack/memory bounded but not deadline bounded.
- Windows compatibility shim is authored, not qualified on locked Windows kit.
- Create non-string/non-array fileMustInclude validation and guessFileIdx options
  remain incomplete. Query plus-as-space decoding is not yet source-complete.
- M03/M04 integration tests include documented liveness deviations in their own
  receipts; passing integration is not a complete media parity claim.

Reproduce via H01-safe-regex-differential in CTest with SERVER1_NODE_EXECUTABLE
pointing to the stated Node24 runtime, and H01-native-torrent-http or
H01-tracker-torrent-http for native payload/route integration.
