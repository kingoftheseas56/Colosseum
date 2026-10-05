# Arc64 paired seed (authored, not Windows-executed)
The two WAVs are generated three-second tones, not copyrighted narration. Copy includes the unchanged downloaded EPUB seed. The progress INI carries speed 1.5.

IMPORTANT: AudiobookDownloader currently requires absolute index paths. The existing seed copier does not rewrite them. Before launch, prepare a copy with `python prepare_seed.py --app-data-root <exact-tagged-AppData-root> --output <new-prepared-seed-dir>`, then use that copy for the matching tag. Never point it at live user data. The raw template deliberately cannot falsely pass the attached wait. No native path-rebasing feature is introduced in this milestone.

ReaderShell.healAudioPairing() can create a missing pairing from the downloaded pairKey; a pre-seeded Windows registry pairing is not required. The bridge does not expose a named hover/wake action for sleeping bars, so timer clicks need the stated human-witnessed flow. Full seed attachment/speed restore remains reviewer verification.
