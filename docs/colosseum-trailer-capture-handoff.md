# Colosseum Trailer — Merged Handoff (corrected: Lane 2 + Lane 3 → capture layer)

Updated 2026-08-26. This **supersedes** the earlier `colosseum-trailer-capture-handoff.md`,
which was wrong about the renderer. Documentation only — decide with the owner
before implementing.

> **Read this correction first.** The campaign trailer is **NOT** built in
> `Colosseum-Trailer-Remotion`. That project is a separate, already-finished,
> stand-alone brand trailer and is not the target of this pipeline. The campaign
> renders through the **existing template system** in the marketing worktree.

---

## 1. The real target system (already exists, and is most of the way done)

Everything lives in the marketing worktree:

```
C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\.worktrees\codex-colosseum-marketing-phase1\marketing\
```

Three layers, tied by one contract:

| Layer | Location | State |
|---|---|---|
| **Capture** | `captures/` — `manifest.json`, `master-shotlist.md`, `rights.md`, human `Invoke-ColosseumHumanCapture.ps1` | **All 10 shots `not-captured` / fallback-bound.** No accepted production-GUI footage. |
| **Render** | `trailer/template/` — `render_template.mjs`, `cards/*.json`, `tokens.json`, `layouts.json`, `schema/`, `manifests/` + `validation/` + `tests/` | **Complete and tested.** Deterministic FFmpeg renderer, 8 cards, 4 aspect profiles, caption gen, QA suite. |
| **Release pkg** | `trailer/release/` — `index.json`, `RENDER_LEDGER.md`, `APPROVAL_CHECKLIST.md`, contact sheets (Task 9) | Packaged for Hemanth review; release fail-closed. |

The pipeline is: **capture → (review/accept + rights + claims) → swap
`source.type` to `clip` in a template manifest → `render_template.mjs` →
watermarked internal preview** (release stays gated on Hemanth).

Canonical plans to read before touching anything:
`docs/superpowers/plans/2026-08-25-colosseum-trailer-production.md` and the
`.superpowers/sdd/2026-08-25-colosseum-trailer-production/*` task reports
(esp. `task-9-brief.md`). `marketing/GLM-HANDOFF.md` is the authoritative
continuation boundary (Task 9 = packaging; Task 4 = the unresolved capture).

## 2. What Lane 2 and Lane 3 really are

Both lanes are **candidate automated backends for the `captures/` layer** —
alternatives to the human one-click capture kit, to produce the 10
`production-GUI` shots:

- **Lane 2 (QML pipeline):** `Preflight-Architect\scripts\build-colosseum-trailer.ps1`;
  drives Lanista `session run` with `--capture-width/height/out`
  (2560×1440 @ 15fps, Lanista's own scene-grab). Home scene proven (`QML_CAPTURE_OK`);
  CWD bug + VS-build hardening already fixed.
- **Lane 3 (Night Watch):** `C:\nwcap\scripts\trailer_capture\run_trailer_capture.py`;
  drives Lanista `session run` (isolated session, tagged appdata, `--drive`)
  **+ OBS Studio `window_capture`** at 1920×1080 @ 60fps, cursor hidden.
  12-shot catalogue (`shots.json`), in-runner probe + `evidenceClass`
  (`production-ui`/`fixture`) + contract test. Only `01-home-shell.mp4` captured
  so far (17 runs; best = 1.3MB real file).

## 3. The merge (recommended)

**Adopt Lane 3's harness as the automated capture backend**
(`run_trailer_capture.py`: Lanista + OBS window_capture 1920×1080@60fps,
evidence-classified, isolated disposable session, no network egress), because:

- It already solves the real capture problem: **OBS `window_capture`** records the
  (disposable) Colosseum window at a constant 60fps and does not need focus or
  desktop-change frames — the exact reason Lane 3 rejected `ddagrab`.
- 1920×1080 matches the required fixed capture environment in
  `captures/master-shotlist.md` ("1920×1080, 100% DPI, dark theme, fixed
  non-personal wallpaper, notifications disabled, hidden cursor, muted
  application audio, two-second settle"). Lane 2's 2560×1440@15fps conflicts.

**Then drop the Lane 3 output into the `captures/` contract, NOT into Remotion:**
for each `captures/manifest.json` shot, produce the clip, record it in the
manifest + `rights.md`, get `accepted_ranges` + `review_status` through review,
then flip the matching template-manifest segment from
`{"type":"clip-placeholder"}` to
`{"type":"clip","clip":"marketing/captures/.../shot.mp4"}` and run
`render_template.mjs`. No new renderer. Nothing in `Colosseum-Trailer-Remotion`.

## 4. Conflicts that must be resolved during implementation

1. **Shot list differs.** `captures/master-shotlist.md` defines the canonical
   **10** shots: `home-unified-entry`, `home-continue-cross-worlds`,
   `tankoban-discovery`, `tankoban-reader-resume`, `biblio-books-audiobooks`,
   `theatre-local-playback`, `vault-local-continuity`, `profiles-management`,
   `extensions-management`, `end-card`. Lane 3's `shots.json` defines a different
   **12** (adds `world-switching`, `taskbar-multitasking`, splits some surfaces,
   no `end-card`, no `home-continue`). **Map Lane 3's capture capability onto the
   marketing 10-shot list** rather than the other way around. `end-card` is a
   generated brand asset (rights, not a capture).
2. **Capture format.** Lane 3 = 1920×1080@60fps h264, `draw_mouse=0`. Template
   renderer aspect-fill crops whatever it gets into 1920×1080/1080×1080/
   1080×1350/1080×1920, so the 1920×1080 source is fine. Drop Lane 2's
   2560×1440@15fps (it also doesn't match the fixed-environment safeguard).
3. **Evidence field names.** Lane 3 uses `evidenceClass` ∈ {`production-ui`,
   `fixture`}; the capture manifest uses `P-GUI` / `F-FIXTURE` / `R-RIGHTS` /
   `H-HUMAN`. Map equivalent and **never upgrade** a fixture clip to P-GUI or
   relabel fixture evidence as production-GUI (GLM-HANDOFF rule).
4. **Build/QML identity gate.** Every accepted P-GUI shot must come from the
   exact worktree runtime: executable SHA-256 `a139aa22…` and QML fingerprint
   `7f6b58…` (recorded in `marketing/CLAUDE-HANDOFF.md`). Lane 3 currently points
   at `C:\nwcap`'s own build; that QML/build must match the worktree identity or
   the shot is rejected. Also verify the live main-tree `colosseum.exe` is closed
   first (single-runtime-slot rule) and **never kill an unrelated process**.
5. **Clearance/rights.** Each shot needs `rights_status` cleared (raw-capture
   rights, cleared demo fixtures only, reject unlicensed artwork e.g. One Piece).
   Lane 3's home capture must be re-reviewed against this — the earlier captures
   used the main-tree runtime, not the required worktree identity.
6. **Fixed environment & settle.** Honour master-shotlist safeguards (fixed
   non-personal wallpaper, notifications off, hidden cursor, muted audio,
   two-second settle after each navigation). Do not enter or persist credentials.
7. **Where clips land.** Write under `marketing/captures/raw/<date>/` and record
   ranges/hashes/probe in the capture manifest + `reviews/`. Keep raw rejection
   evidence unchanged.

## 5. Current gap (honest)

- **Capture:** 0 of 10 accepted. All fallback-bound. The only production-GUI
  attempt (2026-08-25) was rejected in full (main-tree runtime, visible One
  Piece artwork, premature end / no clean trailer). Mac: Lane 3 shows a real
  home capture exists, but it must be re-validated against the worktree
  QML/build identity + cleared-fixture gate.
- **Render:** complete + tested (template system, 28 internal MP4s, validation
  suite green). No renderer work is needed except flipping `clip-placeholder` →
  `clip` once captures are accepted.
- **Release:** packaged for review (Task 9), intentionally fail-closed until
  Hemanth approvals + watermark removal + publication permission.

## 6. Ordered next steps

1. Read `GLM-HANDOFF.md`, `captures/master-shotlist.md`,
   `template/README.md`, `captures/manifest.json`, `captures/rights.md`, and the
   canonical plan/`task-9-brief.md`. Re-observe current process/session state
   (do not trust historical PIDs).
2. Confirm the worktree build at `native/build-msvc/colosseum.exe` + its
   `qml-build.manifest` match the recorded worktree executable/QML identities;
   confirm the visible main-tree Colosseum is closed (single-runtime slot).
3. Wire the automated capture harness (Lane 3 runner, 1920×1080@60fps, OBS
   window_capture, isolated Lanista session) to drive each of the 10 marketing
   shots in order, honouring the fixed-environment safeguards.
4. Per shot: produce clip → probe + SHA-256 + frame-aligned in/out ranges →
   review for correct route, clean privacy, cleared demo media, runtime identity,
   claim support → record acceptance in `captures/manifest.json` + `rights.md`.
   Reject unlicensed artwork / personal state / wrong-route / runtime mismatch,
   keeping the exact text-only fallback.
5. For accepted shots only: flip `source.type` `clip-placeholder` → `clip` in the
   relevant `variants/*/manifest.json` (and/or the general master manifest), then
   run `render_template.mjs --profile all` for the affected campaign.
6. Run the QA gates (`validate-template-contract`, `validate-campaign`,
   `validate-audio`, `render-v2-preview`, `render-v2-release-refusal`,
   `render-clip-containment`, `check-variant-text`, and
   `validation/validate.mjs --mode preview`) and reconcile `release/index.json` +
   `RENDER_LEDGER.md`. Release stays fail-closed.

**Definition of done (capture layer):** all 10 shots recorded with
`P-GUI` (+`F-FIXTURE` where required), `R-RIGHTS` and `H-HUMAN` boundaries
respected, accepted ranges reviewed, `source.type=clip` for every replaced
fallback, internal watermarked previews re-rendered, and **no release-mode or
public asset** produced.

## 7. Safety & honesty boundaries (binding, from GLM-HANDOFF + CLAUDE-HANDOFF)

- Keep fail-closed. Every current video stays watermarked
  (`TEMPLATE PREVIEW — NOT FOR PUBLICATION`). Do not remove the watermark,
  release-render, publish, or silently satisfy any unresolved gate.
- Evidence semantics: `P-GUI` (real production GUI), `F-FIXTURE` (test only,
  never upgraded), `R-RIGHTS` (provenance gate), `H-HUMAN` (Hemanth approval).
  Never relabel fixture/headless/source/test/rendered-card evidence as
  production-GUI verification.
- Use only cleared local demo fixtures; reject unlicensed artwork (e.g. One
  Piece) and personal content. No credentials. No auth/sync/Watch-Party/social/
  downloads/piracy-adjacent surfaces.
- Ownership: free-mic, researcher for the harness; Hemanth owns product truth,
  taste, claims, rights, release, and publication. **Silence is not approval.**
- Keep the marketing worktree isolated from `C:\cmf`, `C:\nwcap`, and the dirty
  shared checkout. Do not commit/stage/push/publish. Preserve unrelated dirty +
  untracked files (incl. `x.id)))`). Lane 3 is a parallel experiment — its
  `C:\nwcap` code should be treated as a source to port, not merged canonically
  into the worktree without the owner.

## 8. Explicit correction of the earlier handoff

- **Do not build** a Remotion QML-trailer layer (`render-qml-trailer.ps1`,
  `qml-trailer-*.test.mjs`, a `src/` composition, `public/qml-trailer`).
  Campaign rendering is `render_template.mjs`; that renderer already exists and
  is tested. Creating a second one duplicates an existing, better-reviewed system.
- **`Colosseum-Trailer-Remotion`** (project `ColosseumTrailer`, 1920×1080@30fps,
  2496 frames) is the finished, unrelated brand trailer. Leave it alone.
- **The marketing worktree is NOT out of scope** — it is the destination for the
  captured footage and already contains the templates, renderer, validator, and
  release packaging. The earlier "out of scope" note was an error.

---

### Quick reference — where you'll be working

- Capture contract: `…\marketing\captures\` — `master-shotlist.md` (10 shots),
  `manifest.json`, `rights.md`, `reviews\`, `tests\one-click-capture-kit.tests.ps1`.
- Renderer + templates: `…\marketing\trailer\template\` — `render_template.mjs`,
  `cards\`, `tokens.json`, `layouts.json`, `schema\`, `manifests\`,
  `README.md` ("How Luna substitutes captured clips").
- Campaign manifests: `…\marketing\trailer\variants\*\manifest.json` + `master\`.
- Automated capture harness (adopt): `C:\nwcap\scripts\trailer_capture\` +
  `shots.json` + `tests\trailer_capture\test_contract.py`.
- Reference launcher (Lane 2): `Preflight-Architect\scripts\build-colosseum-trailer.ps1`.
