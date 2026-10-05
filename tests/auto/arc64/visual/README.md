# Optional visual evidence
Normal `qmltestrunner` skips only the three `test_visualEvidence` cases and needs no output folder. Its QML context has no public getenv API; this optional standard QuickTest setup reads `ARC64_EVIDENCE_DIR` and passes only that directory to QML. With no variable, this runner also skips image capture. It creates P1/P2/P3 beneath the explicitly supplied directory. Invalid explicit destinations fail visibly.

```sh
cmake -S tests/auto/arc64/visual -B build-arc64-visual -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build-arc64-visual
ARC64_EVIDENCE_DIR=/absolute/output QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software build-arc64-visual/arc64_m1_visual_runner -input tests/qml/tst_audiobook_offer_button.qml
# Repeat with tst_reader_audio_offer.qml and tst_reader_sleep_menu.qml
```
On PowerShell set `$env:ARC64_EVIDENCE_DIR = 'C:/path/to/output'`, then invoke the runner executable. No product environment-variable API or new third-party dependency is introduced.
