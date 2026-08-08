# Colosseum Living Update Gallery — Plan Review

**Reviewer frame:** `[Scoped helper (Codex), review]` reviewing the plan against the written
Definition of Done in `docs/superpowers/specs/2026-08-08-colosseum-auto-update-design.md`.

**Work reviewed:** `docs/superpowers/plans/2026-08-08-colosseum-living-update-gallery.md`

## Definition-of-Done ledger

1. **MET — permanent monochrome taskbar signal with reduced motion.** Slice 4 preserves the
   permanent Update button, replaces only its update-state gold with silver-white treatment,
   threads reduced motion, extends the taskbar harness, and replays Available badge visibility.
2. **MET — approved living release gallery.** Slice 2 replaces the hero/cards with one full-bleed
   monochrome image stage, Fraunces release/chapter type, Inter metadata, numbered navigation,
   missing-art fallback, and explicit retirement of both card components.
3. **MET — Up to date chronicle and actions.** Slice 3 requires exact `Everything is up to date`
   and `Check for updates` copy in deterministic tests and the isolated UpToDate scenario.
4. **MET — Downloading chronicle, version, bytes, percentage, progress, and Pause.** Slice 1 creates
   a bounded test-only state seam; Slice 3 requires exact 214/315 MB and 68% semantics, the progress
   rail, Pause, and a third isolated Downloading scenario.
5. **MET — published stable release discovery does not block startup.** The plan treats the landed
   post-first-paint service as preserved behavior in Slices 1 and 4; Slice 5 reruns updater and
   clean-release gates rather than rebuilding or moving discovery onto first paint.
6. **MET — explicit resumable download and installation consent.** Slice 3 preserves the existing
   action mapping and forbids actions during unsafe states; Slice 5 replays explicit download,
   interruption/resume, and Restart and update.
7. **MET — authenticated executable and presentation assets.** Slice 4 routes artwork through the
   existing generator/hash contract and performs a missing-art declaration negative control;
   Slice 5 re-downloads and verifies signed draft assets.
8. **MET — honest offline/interruption/corruption/installer-failure behavior.** Slice 3 covers
   state/action behavior; Slice 5 names offline chronicle, interruption/resume, a deliberately
   broken payload, rollback, and failed-target suppression.
9. **MET — successful update reopens on the compiled target version.** Slice 5's completion signal
   requires the relaunched executable to report the expected compiled version after the swap.
10. **MET — user state and cached chronicle survive.** Slice 5 records representative settings,
    progress, owned/downloaded items across all worlds, AppData inventory/store identifiers, and
    cached chronicle version before and after both success and rollback.
11. **MET — prior payload remains recoverable until healthy boot.** Slice 5 checks bounded backup
    retention through shell-ready acknowledgement, exact cleanup, and rollback relaunch.
12. **MET — deterministic, QML, installer, Lanista, and clean-machine gates.** Every slice names its
    registered layer gates; Slice 5 aggregates the full build, updater CTest subset, QML, taskbar,
    data boundary, installer matrix, release tooling, unit gate, three isolated scenarios, and clean
    Windows account journey.
13. **MET — release identity requires no hand editing.** Slice 5 requires version/tag/manifest/
    installer identity from the clean candidate and makes mismatch a failure; the final DoD repeats
    the requirement.

## Edge review

- **Scope creep:** none. The plan explicitly refuses to recreate the landed updater, add channels,
  publish a release, introduce a new network endpoint, or redesign the installer.
- **Correctness and safety:** the only new native seam is compile-time test-only, authenticated-
  chronicle-gated, non-persistent, byte-bounded, and negative-control-proven. All automated runtime
  actions use tagged disposable sessions; public 1.0 is human-witnessed because it predates Lanista.
- **Verification integrity:** all named commands exist in the two ledgers. Slice 3 is honestly
  Bridge blocked until Slice 1 lands. No sleep, invented event, daily-pipe drive, screenshot-only
  pass, or Qt-Test/QML/Lanista layer substitution appears.
- **Aesthetic authority:** HTML and Lanista grabs are exhibits; Slices 2-5 keep the actual QML result
  open until Hemanth records approval.
- **Plan self-check:** five slices contain every required Brotherhood field; placeholder scan is
  clean; component/property/object-name vocabulary is consistent across implementation and replay.

APPROVE — the plan covers every written completion item, preserves the landed security machinery, and makes the three visible states plus clean-machine update journey independently falsifiable.
