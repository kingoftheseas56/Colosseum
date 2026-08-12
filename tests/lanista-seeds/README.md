# The seed zoo — journey contract (Slice J0, 2026-08-12)

One page, binding for every journey built on this directory (starting with J1's six).

## What a "seed" is

A folder under `tests/lanista-seeds/<name>/` holding a **versioned copy of the exact data a real,
diagnosed bug ran against** — never a snapshot of live AppData, never invented/synthetic data
chosen to make a test pass. Each seed carries one `seed.json` manifest, shape locked:

```
{
  "name": "...",
  "version": 1,
  "provenance": { "bug": "...", "date": "YYYY-MM-DD", "dossier": "path/to/handoff.md" },
  "placement": [ { "content": "...", "destination": "roaming" | "local", "relPath": "..." } ],
  "expectedOnBoot": [ { "surface": "...", "property": "...", "value": <exact> } ]
}
```

- **`provenance`** names the bug, the date it was diagnosed, and the dossier (handoff/postmortem)
  that tells the real story. A seed without a real bug behind it does not belong here (see
  Admission rule below).
- **`placement`** names, per top-level content folder in the seed, which AppData root it belongs
  under (`roaming` = `QStandardPaths::AppDataLocation`, `local` = `GenericDataLocation`) and its
  relative path once placed. Kept for schema completeness and forward compatibility: as of commit
  `4ebec25` (2026-08-11), `lanista session run --seed <dir>` already copies the ENTIRE seed tree
  directly into the tagged Roaming AppData root the app itself resolves, so in practice no seed to
  date needs a manual placement step — see `docs/colosseum-lanista-verification.md` ("Seed zoo…"
  section) for the mechanism and its history. If a future store genuinely lives under
  GenericDataLocation, `placement` is where that seed says so.
- **`expectedOnBoot`** names the properties that prove the seed's healed/expected state, each one
  **exact-value waitable through an AVAILABLE bridge command** (`qml-get` / `ui-wait-for`, strict
  equality only — no operators, no compound predicates) on a surface the Lanista ledger's "Named
  automation surfaces" section actually lists. If the state a journey wants to prove is not
  observable through any AVAILABLE command, the honest answer is **Bridge blocked** — name the
  missing capability. Never invent a property that isn't really there.

## What a "journey" is

A journey = **a versioned seed** (never live AppData) + **a scenario JSON**
(`tests/lanista_scenarios/<name>.json`) + **one authoritative completion property per phase**
(strict-equality waitable, matching the seed's `expectedOnBoot` where the phase is "boot") + **at
least one negative control** (an assertion deliberately corrupted so the scenario goes red, then
restored to green — proof the check can actually fail) + **evidence written into the session run
dir** (`artifacts/lanista-sessions/<id>/`, via `lanista session run`).

A journey's scenario runs against an **isolated tagged session** — never the daily app, never a
shared pipe. Use an explicit `--tag` so the Roaming path is known in advance, and always pass
`--seed` an **absolute** directory (a relative one nests under its own path).

## Admission rule

A new seed is admitted **only when a real bug's diagnosis produces one.** The founding example is
`vault-stale-index-v1/`: promoted from `tests/lanista-slice17-seed/` (which stays in place,
untouched, for its existing referencing scenarios — this is a copy, not a move), provenance the
Vault boot-re-derivation stale-index bug (2026-08-11, dossier
`Brotherhood/agents/handoff-vault-boot-rederivation-luna.md`), healed by the boot-time union
republish landed in `e08424b`. Never grow this directory with invented complexity — a seed that
doesn't trace to a real, dated bug and dossier does not belong here.

## A note on face-dependent journeys

Some existing scenarios (`vault_shelves.json`, `vault_door.json`, `vault_identify.json`) assert the
**current** Vault shelves face — the tile-level content view the Vault Browse workstream's Slice 5
retires. A journey built here should not assume that face is stable. J0's own founding journey
(`seed_zoo_smoke.json`) deliberately stays on boot + non-Vault surfaces (`bootSplash`,
`localLaunchState`) for exactly this reason — it proves the seeded boot is healthy without leaning
on a face that is about to change out from under it.
