# Android port: classifications from Hemanth's clarified philosophy

Date: 2026-11-01. Classification only; no app edits, device interactions, builds or publication in this pass. Prepared directly by Codex, without further delegation.

## Governing intent

The existing approved desktop UI is a deliberately crafted product. An Android port preserves that design, information, controls, visual character and capabilities. Its ordinary work is fitting the same experience to the available space and touch input. Convenience, generic mobile conventions or reusable components are not a justification for replacing that design.

The latest approved desktop remains the design authority. Alpha 77844eef is a recovery reference for the drift; it is not permission to overwrite later desktop refinements. I inspected desktop commit 05c961914061a0bf8c0f7f690bc1c3e73923fa8c (Connections grid, typography, spacing and Back action). The desktop checkout is dirty with unrelated work and has not been changed.

Android comparison source: 9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9. The numbered findings are from HANDOFF-CLAUDE-ANDROID-023 and its UI-DRIFT-CHECKLIST; representative implementation was checked in the current shared checkout and the owned migration diffs. This classifies the reported 28 items and shared patterns; it does not claim a completed hunk-by-hunk implementation or fresh runtime verification.

## Classes

| Class | Decision rule |
|---|---|
| KEEP FITTING / REFLOW | Same elements, information, style and actions; change position/order/stacking/wrapping/spacing/viewport bounds/scrolling/safe-area or keyboard accommodation. Keep touch hit regions faithful to the visible control. |
| RESTORE DESIGN / INTERACTION | Replaced visual vocabulary, discarded information, changed typography/icons/shape/copy, replaced menus/panels/sidebars with new surfaces, or altered desktop presentation. Restore the approved components, then fit them. |
| CONDITIONAL BEHAVIOUR | Original behaviour first. A modest adaptation can be considered when the original would look awkward or is demonstrably disproportionate to reproduce. Record the specific obstacle/cost and show the original attempt and proposed adaptation in both orientations before deciding. |
| KEEP / FIX CORRECTNESS | Preserve explicitly requested repairs and backend safety fixes. Repair clipping, overlap, wrong state/labels, invalid metadata and unintended focus without redesigning the page. |
| NEW PHONE UI EXCEPTION | Only a demonstrated do-or-die case can justify new phone UI. Explain why fitting the existing UI fails and ask Hemanth before building. No such exception is established by this audit. |

An internal reusable component is not automatically a new UI. It may remain if it reproduces the approved appearance and interaction. Transparent hit-area helpers and layout measurement are implementation details; a generic card, new sheet or changed iconography is a user-facing substitution.

## All 28 reported changes

| # | Classification | Surface | Decision |
|---|---|---|---|
| 1 | RESTORE DESIGN | Theatre torrent rows | Restore the letter tile, gold quality, bounded title, metadata, audio/language/tag chips, round Play/Copy/Download controls and whole-row activation. Reflow the existing row; remove flat SourceCard presentation. |
| 2 | RESTORE DESIGN | Comic torrent rows | Restore the original tile, STRONG MATCH and other semantic labels/colours, format/range/issues/trust badges, metadata and round download action. Preserve source-selection semantics. |
| 3 | RESTORE DESIGN | Manga volume sources | Fit/reflow the existing approved source row for the phone. The absence of a dedicated phone row does not justify a generic card or a new progress-bar design. |
| 4 | RESTORE INTERACTION | Chapter language | Retain the original dropdown and its styling; clamp its popup, wrap labels and enlarge invisible hit areas/row heights where necessary. Remove the replacement Chapter language sheet. |
| 5 | RESTORE INTERACTION | Chapter pages | Restore the original page selector, fitted to the viewport. Remove the replacement Chapter pages sheet. |
| 6 | KEEP REFLOW | Manga series header order | Moving mode/language/facts/synopsis blocks is permitted. Restore any simultaneous removal, restyling or change of controls; keep content and capabilities. |
| 7 | RESTORE CONTENT AND DESIGN | Reading-room header | Restore COLOSSEUM · TANKOBAN, device progress and synopsis more, with the approved typography and controls. Their positions may change for fitting. |
| 8 | CONDITIONAL BEHAVIOUR | Volume cover-flow | Original enlargement and centring are the default. First try them fitted in both orientations with real covers. Flattening/contain positioning requires evidence that the original looks awkward or disproportionate effort to reproduce, plus an explained comparison for Hemanth. The current flattening is not approved by geometry tests. |
| 9 | KEEP REFLOW | Biblio book element order | Cover, title/synopsis and action blocks may be stacked/reordered. Preserve original styling, controls, information and actions. |
| 10 | RESTORE DESIGN | Biblio shelf headings | Restore the approved title and See All treatment; remove the substituted larger WidgetHeader/chevron treatment. Spacing and wrapping may adapt. |
| 11 | RESTORE INTERACTION | Discover categories | Restore the original category dropdown fitted/clamped to the viewport; remove the replacement Categories sheet. |
| 12 | RESTORE INTERACTION | Discover filters | Restore the existing filter popup and controls. Fit/wrap/scroll them instead of introducing a different filter sheet. |
| 13 | RESTORE INTERACTION | Discover sources | Restore the existing sidebar and its approved phone-overlay behaviour. Remove the replacement Sources sheet and preserve the existing source information. |
| 14 | RESTORE INTERACTION | Row index | Restore the expanding rail, fitted to the phone. Remove the Rows button/On this page sheet substitution. |
| 15 | FIX FITTING REGRESSION | Landscape rail overlap | Fix content reservations/anchoring with the restored rail so it cannot obscure the catalogue. No new navigation surface is required. |
| 16 | RESTORE DESIGN | Profile rows | Restore the original type treatment, drawn chevron, dividers and visual rhythm. Retain only demonstrably necessary spacing, wrapping and touch-area adjustments. |
| 17 | RESTORE SURFACE | Account flyout | Fit the original account flyout; preserve its visual hierarchy and controls. Remove the new Account sheet/title and changed modal presentation. |
| 18 | RESTORE SURFACE | Stremio sync | Fit the existing panel using viewport clamping, scrolling and IME avoidance. Remove the sheet substitution. |
| 19 | RESTORE SURFACE | Add-on setup | Fit the original setup panel and controls. Remove the new sheet presentation and added sheet copy. |
| 20 | RESTORE + FIX FITTING | Tankoyomi configuration | Restore the default-language dropdown; fix provider/control crowding through wrapping/stacking/scrolling with the approved controls, including landscape. |
| 21 | RESTORE INTERACTION | Other dropdowns | Stop replacing ComboBoxes with Choose an option sheets. Retain the approved control and fit its popup. |
| 22 | RESTORE CLASSIFICATION SCOPE | Phone/tablet reach | Restore the approved Android phone reach (alpha uses Android and min(width,height)<600), including landscape phones. Keep viewport/safe-area/IME measurement work. Do not treat every touch device or sub-840 tablet as a phone. |
| 23 | RESTORE CONTROL VISIBILITY | Library Select | Restore the existing phone-only gate; do not introduce phone-only controls on all Android tablets. Preserve existing selection capabilities and canonical state. |
| 24 | RESTORE DESIGN AND DESKTOP | Carousel indicators | Restore approved drawing, stretch animation, focus/tab behaviour and desktop hit geometry. Add phone-only invisible hit padding if required without changing their appearance. |
| 25 | KEEP CORRECTNESS FIX | Featured Read/Details | Keep the explicitly requested route repair, including its agreed desktop correction. It restores the existing action rather than adding a new design. |
| 26 | KEEP CORRECTNESS FIX | Reader Back to series | Keep the explicitly requested return-to-originating-series/session repair. It is an accepted functional correction, not authority for restyling reader controls. |
| 27 | KEEP PLATFORM FIT | Android window buttons | Keep desktop minimize/fullscreen/close controls hidden on Android; preserve desktop controls. |
| 28 | FIX REGRESSIONS | Invalid seeders/autofocus/tablet filter scroll | Do not display unknown seed counts as -1 or invent zero. Restore intended focus so opening Stremio does not summon the keyboard without intent. Make tablet filters scroll/reflow while preserving their existing design. |

## Shared mixed changes that need the same treatment

- Keep pure viewport measurement, keyboard coordinate conversion, safe-area accommodation, scrolling, wrapping and faithful touch-target work. These do not justify a new presentation.
- Preserve approved font faces, weights, colours, icons, borders, shapes, animations and information hierarchy. Size accommodations require an actual fitting/readability reason; they must not become a different typography design.
- Restore the original desktop menus, drawn icons, carousel interaction, narrow-window behaviour and layout wherever the port changed them. Preserve the separately requested 25/26 fixes and later approved desktop work.
- CollectionMenu/CollectionCardMenu, PhoneListItem/PhoneActionRow, TouchTarget and shared hosts are judged by their visible result and interaction, not by their filenames. Restore altered UI or behaviour; retain faithful technical reuse.
- Remove sheet hosting from the restored account/Stremio/setup surfaces. A sizing/IME fix such as b2255042 may be retained only where its implementation still serves the approved original surface; it does not authorize keeping the sheet design.
- Native player landscape lifecycle, codec correction, torrent cancellation/demand retirement, release identity and restoration of the existing launcher icon are separate correctness/platform work. Preserve them while restoring presentation.
- Player/reader/bespoke-universe ownership stays with Claude. Their controls still follow the same fidelity principle; this classification is not authorization to edit those protected files.

## Execution boundary

Apply classifications per hunk/state. Do not revert entire files or the whole Android lane: that would also remove valid fitting, requested route fixes and native safety work. Preserve unrelated and ongoing desktop changes.

For a future correction, show each affected page upright and sideways with populated content. Behaviour exceptions need the original attempt and proposed version; desktop-sensitive changes need the corresponding desktop comparison. This document records required evidence, not evidence already produced. Phone testing remains with Hemanth unless he separately authorizes agent operation.

The current beta remains held because of reported UI regressions and the need to review the restoration. OnePlus testing alone does not clear the publication hold. No public uploads, preview deployments, tags or releases are authorized.
