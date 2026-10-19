# M304/M663 native file-guessing qualification

This receipt qualifies the native helper, not the full server or HTTP integration.
The source is the locked Stremio 4.21.1 bundle with SHA256
`405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.
The test runner extracts the unmodified M304 and M663 factories using the locked
module index and verifies both extracted hashes before execution.

## Results

- Optimized GCC build: 24 offline golden cases and 92,892 differential cases.
- ASan/UBSan: 24 golden cases and 21,080 differential cases on Node 24.19.0.
- Explicit portable Node 22.16.0 oracle: 21,080 differential cases under ASan/UBSan.
- The default runtime gate rejects Node 22 unless `--allow-portable-node22` is supplied.
- Leak detection is a separate gate. The executor reports that LeakSanitizer
  cannot run under ptrace; ASan/UBSan qualification uses `detect_leaks=0`.

See the raw logs and `receipt.json` for counts, exact versions, hashes, and gate
status. `source-inputs.sha256` identifies every native/test source compiled or run,
including the shared Value/conversion sources. `source-unchanged.log` verifies
that these inputs did not change during qualification.

## Reproduce

From the repository root:

```sh
bash artifacts/server1/H01/GUESS-20261019/run-receipt.sh
```

The script contains the exact compiler arrays, flags, environment, and test
commands. It defaults to the existing adjacent Qt 6.11.1 dependency tree and
locked oracle pack. Override `QT_ROOT`, `NODE24`, `NODE22`, `ORACLE`, `INDEX`, or
`BUILD_DIR` for a different local layout. It rebuilds only out-of-tree binaries
and rewrites this receipt; it does not edit production sources.

Default 21,080-case oracle command, from the repository root after the script has
built its binaries:

```sh
python3 native/colosseum_server_v1/tests/run_guess_file_index.py \
  --candidate /tmp/server1-guess-receipt-20261019/guess_file_index \
  --oracle ../locked-inputs/execution-v2.1/evidence-pack/oracle/stremio-service-v4.21.1-server-bundle/server.js \
  --index ../locked-inputs/execution-v2.1/MODULE-INDEX.json \
  --node node
```

Append `--random-cases 40000` for the optimized 92,892-case corpus. Portable Node
22 requires `--node ../deps/node22/node-v22.16.0-linux-x64/bin/node` together with
`--allow-portable-node22`. Node is an oracle-only test dependency; production uses
native C++ and Qt Core.

## Scope and remaining gates

M304 implements the complete default-options metadata control flow used by M663,
including title/type/date gates, folder overwrite order, compact numeric stamps,
sample handling, and deletion of season/episode. Fixed regexes explicitly account
for ECMAScript ASCII case matching, line terminators, and non-multiline anchors.
The differential suite compares complete metadata objects as well as selections,
strict season/episode types, relational length coercion, all media suffixes, and
targeted TypeError messages.

Alternate M304 options, JavaScript getters/functions/proxies outside the
`policy::Value` data model, parent route/full-host integration, and Windows are not
qualified by this receipt. Passing the corpus is behavioral evidence for this
scope, not a claim of exhaustive input-space proof.
