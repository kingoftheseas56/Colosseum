# Aqueduct Wave 0 — frozen oracle and regression corpus

Wave 0 is research and harness work only. It does not change Colosseum's production streaming path.

## Packets

- **W00:** pin the exact Stremio Server 4.20.17 specimen and close the source-to-port matrix.
- **W01:** provide a reproducible oracle runner that captures Stremio behavior before C++ exists.
- **W02:** turn Tankoban 2's failed native streaming history into named regression obligations.

## Exact oracle

- `server.js`: 6,631,104 bytes
- SHA-256: `567A397BB11B788571BF1750FD05DD78927F97BEC0C9DDEAA6D9CC1ECCEE3922`
- `stremio-runtime.exe` SHA-256: `8AD810919DF76741A153DBF28180A84F7AB395EA3DA2534374A10F0E6DCA7E3B`
- server version: `4.20.17`

The hashes match both the 2026-08-07 forensic specimen and the payload currently packaged by Colosseum.

## Safety model

The oracle runner never executes the installed payload in place. It copies the complete stream-server directory into `native/build-msvc/_aqueduct-wave0/`, verifies the hashes, then patches exactly six ASCII `11470` literals to `11480` in the lab copy. The retry ceiling remains `11474`, so a busy `11480` fails instead of falling into the production port range.

The runner removes `NODE_OPTIONS`, sets `NO_HTTPS_SERVER=1`, gives Stremio a private `APP_PATH`, and verifies that the lab port closes after shutdown.

## Commands

```powershell
python docs/research/aqueduct-wave0/verify_wave0.py
python docs/research/aqueduct-wave0/oracle/capture_oracle.py control
python docs/research/aqueduct-wave0/oracle/capture_oracle.py offline
python docs/research/aqueduct-wave0/oracle/capture_oracle.py live
```

`control` and `offline` are deterministic local suites. `live` uses the fixed legal Sintel torrent and is evidence, not a deterministic CI gate.

## Outputs

- `UPSTREAM-AUTHORITY.md` — specimen, provenance and dependency pins.
- `SOURCE-PORT-MATRIX.csv` — functional source surface to future W-packet ownership.
- `TANKOBAN2-REGRESSION-CORPUS.md` — historical failures turned into mandatory future gates.
- `oracle/golden/*.json` — normalized Stremio reference captures.
- `receipts/WAVE0-RECEIPT.md` — executed verification record.
