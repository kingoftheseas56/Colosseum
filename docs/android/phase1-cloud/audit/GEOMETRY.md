# Sources and installed overlay geometry

The Sources result area can become too short even while the app continues accepting input. The source establishes a height-budget problem; it does not establish the cause of every reported scrolling pause.

## Exact owner and equation

Committed Beta501b23edba022a82666bd25e64f4f02ad8bb9a9f `qml/Main.qml:5270-5276` hosts a full-parent Loader choosing `TheatreSeries.qml` by default (`viewSettings.rebuiltTitlePages=false`, Main:582-587) or `TheatreSeriesView.qml`. Each title root fills its Loader. The direct `SourcesSheet` children at legacy2437/rebuilt2248 inherit `anchors.fill:parent` from SourcesSheet:19. This sheet is not inside WorldPage's vertical content and does not subtract taskbar or WorldPage insets. Actual device rectangles were not queried.

For `rows.length>0`, `visibleRows.length>0`, desktop branch, with one quality-pill row, committed SourcesSheet anchors give:

| Element | Logical units | Committed lines |
|---|---:|---|
| banner |300|356-359|
| gap before extension filter |20|432|
| filter |64|433|
| gap before quality Flow |14|486|
| one quality row |40|506|
| gap before table |16|543|
| table bottom margin |26|544|
| table head |52|551|
| list top/bottom margins |4+8|639|
| Total reserved |544| |

Therefore anchored list height is **sheet height−544** before rounding. Additional Flow rows add40+12=52 each. Source delegates are150 units high (685); a96-unit viewport can scroll but cannot show a complete row at once. Empty/all-filtered states have different geometry; do not apply the same equation blindly.

## Conditional zoom calculation

`DesktopPresentationScale.java:7-13` computes `min(1,W/(density*1024),H/(density*640))`, rounded down when less than1. Activity:105-145 establishes this startup `QT_SCALE_FACTOR` before Qt onCreate, with API30+ drawable bounds excluding display cutout. This is not a QML `scale` transform. The receipt-verified Activity overlay in `platform-evidence/ColosseumActivity.java:120-141` multiplies this factor by visualZoom, default1.15 or finite cold-launch extra `colosseum_visual_zoom` in0.9..1.3; supplied CURRENT.json records1.20. `onNewIntent` does not rerun startup scale.

**If** the scale is below1 and height-limited, drawable bounds/base density match Qt's client geometry, and no further window/inset difference changes it, the reference logical height is640, and zoomed height approximately640/zoom. These are calculated examples, not current measured phone viewport values:

| Launch scale multiplier | Conditional logical height | One-row list budget |
|---|---:|---:|
| clean committed1.00|640|96|
| overlay default1.15|556.52|12.52|
| recorded override1.20|533.33|−10.67|

Width-limited or scale-capped devices require their own calculation. Negative anchor budget indicates incompatible constraints; this audit did not instantiate Qt to measure clamping/rendering. Insets, density rounding, IME/window resizing and actual orientation must be recorded before a runtime claim. A T20 cannot be declared incapable from this formula.

Because Qt receives one startup coordinate scale, ordinary Qt item hit testing should remain in the same logical coordinate system; the source does not show a separate visual-only transform that would prove misaligned taps. Fixed logical gesture thresholds/edge exclusions and tap areas do change physical size with the factor. Native Android WebView/Media3 overlays and Android system gesture coordinates cross separate bridges, requiring device verification. No gesture ownership or event dispatch change is established merely by multiplying the scale.

## Eight overlays

All hashes are recorded in `package-verification.json`; seven APK QML byte hashes and preserved Activity source match packaging receipt. Main/WorldPage/AdaptiveLayout remove54-unit outer content gutters and use16-unit header insets; Sources only changes horizontal margins; WidgetHeader/ContinueRow add8-unit See-all right inset; Activity multiplies scale; HouseScrollBar changes Android policy toAlwaysOff. The statement “margin/zoom only” is incomplete because the scrollbar override removes an affordance.

HouseScrollBar committed policy:17 shows track when content overflows; its `interactive:true` at18 supports thumb/track interaction. Overlay:17 makes policyAlwaysOff onAndroid; track/content are expressly invisible forAlwaysOff. Active sites include Main:4279, WorldPage:297 and SourcesSheet:652. Flickable owners, KeyboardCollectionController and KeyboardScrollController are unchanged by this overlay. Removing the scrollbar does not fix the height budget; source alone does not prove it intercepts touch or disables keyboard scrolling. Current rendered/accessibility exposure is unverified.

## Evidence chronology

Package receipt statusPACKAGED_VERIFIED_NOT_INSTALLED/device_or_installNOT_PERFORMED is packaging evidence. Later supplied touch-integration/CURRENT.json records normal update/hash match, OnePlus11R CPH2487 Android16 and cold-launch1.20; it also withdraws an earlier player-test confirmation. Later corrected human dialogue says Beta comic reader works with different controls and consumption functionality present. It does not fill the detailed DEVICE_ACCEPTANCE matrix or qualify NokiaT20. AlphaDesk comparison lacked the proposed20percent enlargement, so it was not a controlled zoom-parity experiment. Beta remains selected foundation.
