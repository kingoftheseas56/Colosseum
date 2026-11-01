# Batch 1 — decisions and exact remaining work

## D1 / B1-012: readable volume flow at constrained phone heights

**Decision owner: Hemanth. Status: proposal implemented and browser-verified; adoption not approved.**

The supplied brief requires the original centered/enlarged volume flow to be tried before an alternative. The finished comparison preserves that behavior and its controls. The fitting obstacle is measurable: the constrained attempt drops below the frozen source's 190 px cover minimum in six tested cases. It reaches only 16 px in short landscape and both text/inset stress cases.

The proposed alternative retains the 190 px minimum, the 1.10 focused scale, neighboring scale falloff, full caption, original count/selection lane and bottom actions. It allocates enough vertical space to the existing content and lets that content scroll. At the tightest stress case, the chrome must join the scroll because keeping it fixed leaves less space than the enlarged cover and caption require.

### Inspect the concrete choice

- [Interactive original-attempt / proposal comparison](finished-mocks/compare.html?pair=flow)
- [Same comparison at tight landscape stress](finished-mocks/compare.html?pair=flow&preset=short&font=1.3&insets=24&position=gallery)
- [Constrained original attempt](finished-mocks/05-volume-flow/volume-flow-original-fit.html)
- [Proposed readable alternative](finished-mocks/05-volume-flow/volume-flow-minimal-alternative.html)
- [Portrait decision sheet](contact-sheets/decision-flow-portrait.png)
- [Tight landscape decision sheet](contact-sheets/decision-flow-stress.png)
- [Untouched original worker files](../../outputs/05-volume-flow/index.html), also individually linked from the finished index

“Original attempt” above means the corrected comparison copy: shared icon, focus, caption and state defects have been fixed while the constrained allocation remains visible. The untouched worker original is separately preserved and linked. Neither a small-cover failure nor an unapproved proposal is silently presented as accepted product behavior.

### Measured result

Cover height is the **unscaled logical height**, before 1.10 focus enlargement. Scroll is the measured content offset at the tested full-gallery view, not the maximum scroll extent. Top/bottom insets are illustrative CSS pixels.

| Viewport | Text | Insets top / bottom | Original cover | Proposed cover | Proposal gallery scroll | Chrome scrolls |
|---|---:|---:|---:|---:|---:|---|
| 412 × 900 | 1 | 0 / 0 | 139 | 190 | 56 | No |
| 900 × 412 | 1 | 0 / 0 | 16 | 190 | 265 | No |
| 354 × 792 | 1 | 0 / 0 | 66 | 190 | 136 | No |
| 792 × 354 | 1 | 0 / 0 | 16 | 190 | 282 | No |
| 1280 × 700 | 1 | 0 / 0 | 210 | 210 | 0 | No |
| 800 × 1280 | 1 | 0 / 0 | 276 | 276 | 0 | No |
| 1280 × 800 | 1 | 0 / 0 | 276 | 276 | 0 | No |
| 354 × 792 | 1.3 | 24 / 24 | 16 | 190 | 235 | No |
| 792 × 354 | 1.3 | 24 / 24 | 16 | 190 | 350 | Yes |

The proposal's focused cover/caption was fully visible at the saved gallery position in every row. The action bar is also reachable, with a separate saved position where needed. In the tight landscape case the full cover and all bottom actions cannot occupy the same short viewport at once. This cost is shown in the evidence rather than hidden through cropping or scaling down the original component.

At 1280 × 700 and the two tablet orientations, the proposal requires no gallery-position scroll and keeps the corresponding original geometry. This is a conditional fitting adaptation; it does not replace the desktop presentation.

**Recommendation:** select the proposed allocation if preserving the native cover size and center emphasis takes priority. If Hemanth rejects scrolling chrome at the tightest size, the exact remaining choice is how to reclaim that vertical space while retaining the original elements. A flattened flow, replacement grid or new phone surface was not approved or implemented.

### Unaffected work

The source-picker, confirmation, chapter, metadata, focus and desktop-fidelity fixes are complete and independently usable. The seven primary page targets have fresh final verification. The two flow artifacts contain complete working controls and evidence; the constrained original remains an intentional comparison limitation.

## Source composition: resolved for this pass

The default Main.qml flag selects MangaSeries.qml. The finished series/chapter target uses that default shared header / reading-room / chapter-ledger composition, plus the brief's explicit wordmark, progress and more/less restorations. This is the chosen source-based implementation, not a request to switch to the optional rebuilt route.

The retained “Optional rebuilt components” selector is labeled comparison-only. It does not reproduce TitlePageBar and must not be adopted as a complete route. Implementing a full rebuilt route is not unfinished Batch 1 work: that route change was not selected or authorized. See [QML-PORTING.md](QML-PORTING.md).

## Existing input gap: B1-003

The original outputs/05-volume-flow/REPORT.json was absent at the start and remains absent. It was not fabricated or written back into the worker output. Fresh review captures, per-page checks, hashes, contact sheets and this batch's final REPORT.json supply a usable replacement handoff for the corrected copies. The original worker provenance gap stays recorded.

## Artwork limitation

The corrected pages use supplied project art, fonts and icon paths. The local inventory did not find a corresponding manga jacket/chapter image set; One Piece illustrations and film posters serve as explicitly disclosed stand-ins. The exact matching images remain unavailable, not generated or borrowed from the web. Replacing these fixtures requires approved matching artwork; the layouts and no-cover fallback are already runnable.

## Work that remains outside this HTML assignment

1. Hemanth selects or rejects D1 using the two runnable flow versions and the stress evidence.
2. Codex ports the selected fitting into the original components identified in QML-PORTING.md. No repository or app change occurred in this pass.
3. When separately authorized, verify the actual QML runtime: font metrics, safe areas/system bars/input methods, real accessibility/focus, neighbor-tap event order, and service-driven acquisition/consume/cancel behavior.
4. If original worker handoff provenance is needed, obtain the missing worker report from its owner; do not mistake the new report for an original worker artifact.
5. Supply exact matching manga artwork if required for content acceptance.

There are no final HTML fixes marked “fixed but unverified.” The constrained original's six sizing limitations and D1's product approval remain explicit. Native runtime and service behavior are unverified because they were outside this assignment, not silently counted as passing.
