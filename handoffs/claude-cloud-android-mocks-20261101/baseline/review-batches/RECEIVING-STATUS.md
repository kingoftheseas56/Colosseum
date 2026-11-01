# Receiving status — 2026-11-01

Primary reviewed the Sol receiving reports, decisive source references, defect screenshots, correction diffs and focused verification evidence. The dispositions below apply to HTML mock handoffs and isolated corrected candidates, not native application implementation or release acceptance.

| Batch | Primary disposition | Use next |
|---|---|---|
| 1 — sources and manga | Five source-picker/confirmation pages suitable for visual review. Hemanth chose the rebuilt series/chapter content. Its real title bar and refreshed flow/header comparison still need completing. | [User decision](01-sources-manga/codex-review/USER-DECISION-REBUILT-MANGA.json); recipient changed to Claude Code Cloud, packet prepared, NOT DISPATCHED. |
| 2 — detail pages (8) | Receiving review complete. Accept the scoped Android-window-menu correction as an HTML candidate; keep Theatre route selection explicit. Six unaffected pages remain usable with disclosed gaps. | [Review](02-detail-pages/codex-review-sol/REVIEW.md), [corrected overlay and patch](02-detail-pages/codex-review-sol/REMEDIATION.md). |
| 3 — browsing/libraries (16) | Receiving review complete. Accept the corrected Search candidate; 15 other pages remain usable with disclosed gaps. | [Review](03-browsing-libraries/codex-review-sol/REVIEW.md), [corrected Search and patch](03-browsing-libraries/codex-review-sol/REMEDIATION.md). |
| 4 — accounts/configuration | Not reviewed in this pass. No completion or acceptance inferred. | Existing assigned batch. |
| 5 — tracker/utilities (7) | Receiving review complete. Accept the scoped tracker-focus correction as an HTML candidate; the other six pages remain usable with disclosed gaps. | [Review](05-tracker-utilities/codex-review-sol/REVIEW.md), [corrected overlay and patch](05-tracker-utilities/codex-review-sol/REMEDIATION.md). |

## Accepted corrective candidates

- Batch 2: omit desktop Window trigger/actions from Android phone/tablet DOM and focus traversal, retaining Back/Search and desktop actions. Sixteen focused page/renderer/host cases passed. This does not choose between the two Theatre compositions.
- Batch 3: retain query/caret through debounce and submission, respect intentional focus moves, and follow the existing local top-result intent. The initial corrected query implementation passed 72 focused checks. Primary then identified duplicate desktop action identities; the final exact-label identity correction passed nine targeted checks, including pointer/keyboard focus and a query smoke. Those are separate candidate/checkpoint results, not 81 full checks on one revision. Native navigation must not forcibly restore hidden Search focus merely because the fixture remains on its page.
- Batch 5: restore the exact dossier opener and nested-dialog invokers, with visible/enabled fallback and no focus theft during deliberate external navigation. Seventy-eight focused scenarios passed across phone orientations and desktop.

All three corrections remain in isolated reviewer-owned candidates/overlays. Original Pro mocks and initial receiving reports are preserved; opening their original index still loads their original JavaScript. Use the linked corrected candidates/patches as the implementation reference for affected files.

## Decisions and limits

Theatre legacy versus rebuilt selection remains distinct from Hemanth's manga choice. Matched [portrait](02-detail-pages/codex-review-sol/theatre-both-portrait.jpg) and [landscape](02-detail-pages/codex-review-sol/theatre-both-landscape.jpg) comparisons are available; the Android menu defect shown in the original rebuilt branch is fixed in the separate candidate.

For B2-018, the primary inspected the original and conditional Universe tab placements. Moving the same tab pills below Back to avoid measured portrait overflow is consistent with the permitted layout/reflow category; it does not require a new-phone-UI exception. The reviewer recommends that conditional fit and retains both variants for Hemanth's visual review. No native adoption is implied. Batch 3's optional poster-bleed presentation is not selected by this receiving pass.

The native app, QML repositories, installed devices and publication state were not changed. HTML fixture behavior is not provider/service success or native/device qualification. Exact artwork, Windows/Qt metrics, actual insets/IME, persistence and native route/focus behavior retain their stated receiving gaps.

Reviewers were requested through real dispatch tools as gpt-6.1-sol / medium; packaging was requested as gpt-6-luna / max. Effective model/effort metadata was not exposed by the dispatch result. Primary acceptance is based on the supplied work and evidence, not model labels.

## Dots handoff

**Recipient changed by Hemanth to Claude Code Cloud attached to the Colosseum GitHub repository.** Use the [Claude Cloud packet](D:/Colosseum-ClaudeCloud-Manga-Rebuilt-20261101/CLAUDE-CLOUD-MANGA-REBUILT-20261101.zip) and [Claude task brief](D:/Colosseum-ClaudeCloud-Manga-Rebuilt-20261101/PROMPT-CLAUDE-CLOUD.md). Luna adapted the instructions and resealed the packet; all 465 baseline input files remain byte-identical to the accepted Dots packet. The new contract explicitly retains Android phone/tablet gating of desktop window actions. Primary reviewed the adapted scope and packaging validation; it is ready to supply to Claude Cloud, not dispatched through this chat. The Dots-specific packet below is preserved as the earlier transport version.

[Packet ZIP](D:/Colosseum-Dots-Manga-Rebuilt-20261101/DOTS-MANGA-REBUILT-20261101.zip) · [ready-to-paste full task](D:/Colosseum-Dots-Manga-Rebuilt-20261101/packet/PROMPT-DOTS.md) · [packaging validation](D:/Colosseum-Dots-Manga-Rebuilt-20261101/PACKAGING-VALIDATION.json).

Primary accepts the package as ready to attach. Luna verified CRC, all 470 declared file sizes/hashes, source equivalence at the fixed pin (including explicit CRLF-only text distinctions), source freshness, bounded dependencies and exclusions. Local-only Windows fonts and selected historical raw evidence are explicitly omitted. Dots execution has NOT started through this chat; the packet needs delivery to Dots. No direct Dots dispatch tool is exposed in this session.
