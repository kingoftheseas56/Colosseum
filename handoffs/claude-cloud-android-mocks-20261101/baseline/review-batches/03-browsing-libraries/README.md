# Batch 3 — finished browsing and library mocks

The user’s updated implementation scope supersedes the original review-only README. The original batch instructions are preserved in [evidence/README-ORIGINAL.md](evidence/README-ORIGINAL.md). Original worker outputs and frozen references remain unchanged.

## Start here

Open [index.html](index.html) for all **16 finished pages**, links to every preserved original, live before/after comparisons, and saved contact sheets. [finished-mocks/index.html](finished-mocks/index.html) is the direct mock entry point. Each finished worker folder also has an index redirect to this entry point.

The corrected HTML lives only under `finished-mocks/`. Assets resolve to the existing frozen project artwork, icons and local fonts under `../../reference/`; keep this folder in its assigned project location. No package installation or application build is needed.

The pages can be opened locally. To use a local HTTP origin, serve the existing project root with any static server. For example:

```powershell
python -m http.server 8765 --bind 127.0.0.1 --directory D:\Colosseum-Android-Phone-Mocks-20261101
```

Then open `http://127.0.0.1:8765/review-batches/03-browsing-libraries/index.html`. The live comparison uses the original workers’ inspector controls when older workers ignore stress URL parameters. A local HTTP origin avoids browser security restrictions on cross-file iframe inspection.

## What is included

- [REVIEW.md](REVIEW.md): outcomes, remaining limits, verification and handoff.
- [QML-PORTING.md](QML-PORTING.md): original components, source pins, concrete fitting rules and signals.
- [COVERAGE.csv](COVERAGE.csv): one row per page, with verification and evidence.
- [FINDINGS.csv](FINDINGS.csv): stable grouped findings and disposition.
- [decisions.html](decisions.html): P1, the source-faithful narrow poster hero and an optional artwork alternative for Hemanth.
- `contact-sheets/`: original, finished, stress/tablet and before/after sheets.
- `evidence/`: original/final input hashes, screenshots and metadata, focused QA, capture scripts and checkpoints.
- [REPORT.json](REPORT.json): final machine-readable completion record, written last.

## Fixture boundaries

Actions operate on local demonstration data. Owned navigation routes connect the finished pages and retain relevant return state. Details, players/readers, general Home, accounts and live providers remain original intents at the batch boundary; they do not perform host or service operations. Reload resets local mutation fixtures, except state explicitly carried by an Explore return URL.

All artwork is from the frozen project bundle. Some titles have representative saga/universe artwork rather than their exact cover; these are declared fixtures, not validated catalogue identities. The optional narrow poster bleed is not approved or selected by default. The selected default follows CarouselSlide’s current poster branch.

Viewport dimensions and the 24px inset examples are provisional. These artifacts are verified HTML fitting studies, not Qt runtime, physical-device or product acceptance evidence. Final product decisions remain with Hemanth and Codex.
