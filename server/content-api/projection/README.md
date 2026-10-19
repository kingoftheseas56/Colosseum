# Compact catalogue preparation

This is a local projection builder and bounded D1 qualification helper. It does
not publish a revision, change native consumers, disable the legacy downloader,
or declare the 1.1.9 release ready. Existing Tankoban publishing stays separate.

Run from `server/content-api` with Python 3.11 or newer and an SQLite runtime
supporting FTS5 trigram. All four frozen inputs must match `source-manifest.json`:

```powershell
python projection/prepare.py --source-dir <frozen-catalogues> --output-dir <fresh-artifact-directory>
python projection/verify.py --source-dir <frozen-catalogues> --artifact-dir <artifact-directory>
```

The builder opens sources read-only, checks their hashes before and after,
rejects active journal/WAL files, and refuses to replace an existing artifact.
The verifier checks every original non-Tankoban record and its multiplicity,
detail ownership, complete UTF-8 payload envelopes, pagination and representative
SQL contracts. Production contract fixtures use the manifest-qualified snapshots;
small test fixtures do not establish production completeness.

Outputs are `projection.sqlite`, `manifest.json` and, after verification,
`verification.json`. Keep these generated data files outside the repository.
The revision includes source identities, page cap and the exact builder bytes;
cross-platform line-ending normalization is not qualified.

`contracts.build_contracts()` exposes source/projection SQL, parameters, ordered
scalar expectations and explicit remaining native gaps. It does not implement
request-dependent native curated ranking or QVariant/QML conversion.

`qualify.prepare_probe()` prepares a deterministic plan with bounded root,
membership and payload samples, indexes and the actual external-content trigram
schema. `qualify.execute_plan()` accepts an injected authenticated executor and
an independently reviewed plan digest. It stops on missing meters, budget
overruns, provider errors or scalar mismatch. Callers own authenticated transport
and verified cleanup of their disposable database; use `compose_connector_code`
when embedding parameters as a JavaScript literal. Never use replacement-string
substitution for catalogue data.

The helper ceiling is 10,000 reads and 1,500 writes. Across multiple attempts,
subtract all earlier actual costs before authorizing another plan. Unknown costs
are not zero. A sample does not qualify full imports, update costs, query latency,
large payload delivery, Workers CPU or native/offline behavior.

Tests (Node must be on PATH for the actual JavaScript transport regression):

```powershell
$env:COLOSSEUM_CONTRACT_SOURCE_DIR = '<manifest-qualified-catalogues>'
python -m unittest discover -s projection -p 'test_*.py' -v
```

Without that environment variable, the two original-snapshot checks are skipped.
