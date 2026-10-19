# Switzer source-distribution route: CLOSED FOR PREPARATION

Candidate 7792bd1d9b3a41185cea522a9984ecd6586e3b44 remains unchanged. Installer remains 333,715,118 bytes, SHA256 ddf011d2955de39e19375d492284867db09d48a80f3b4fe717da086a3b3ec67b. No app/native/QML code, font bytes in the candidate, version, installer or production state changed.

## Outcome and selected route

**CLOSED for source-delivery preparation under the documented independent-font interpretation.** The prior unresolved Switzer scope blocker is superseded by a positive, bounded interpretation on the observed facts, not by free-download status or merely loose paths. The four unmodified, independently authored general-purpose Switzer fonts remain own-UI resources in the unchanged installer under FFL sections 01/02. They are excluded from public covered-source materials as independent works under GPL3 section 5. All covered application/dependency code, source packaging and build recipes remain. Explicitly use LGPL3 section 4(d)(1) / LGPL2.1 section 6(b) suitable shared-library mechanisms, not the alternative application-material route.

The narrow-route assessment explains three distinct questions: installer embedding is expressly supported; raw public-source redistribution is not presumed permitted and is avoided; covered-work scope is an evidence-backed interpretation supported by independent authorship, generic rendering-data semantics and official GNU normal-font-use guidance. It is not a binding legal ruling, a claim FFL is GPL-compatible, or a waiver of direct GPL-3+ libmpv requirements. Fontshare is not a GPL source server or a GPL source exception.

## Verified facts supporting the interpretation

- Exact official font hashes and no modification; general-purpose fonts pre-exist this application.
- Pinned QML uses standard FontLoader URL/name and normal Text font selection. No application-specific font callbacks, bindings, control logic or imported application/libmpv code was identified. Native build resources do not incorporate the fonts; the full font byte streams are absent from the native ELF. Hardcoded filenames and UI metrics are acknowledged, but are normal font-selection behavior rather than intimate program semantics.
- FFL definitions exclude applications made through normal font use from font derivative works. This supports the own-font boundary, but is not itself a GPL licensor's ruling.
- GNU's official license list says GPL-incompatible Arphic licensing poses no problem in its normal font use. GNU aggregate/input-data guidance corroborates the distinction; these are interpretive sources, not permission from ITF or a court decision.
- Actual executable directly links GPL libmpv and Qt/MpvQt shared libraries. Full dependency source obligations remain unchanged. AppRun uses extracted app-private shared-library paths without signing checks; no environmental override is assumed.

## LGPL mechanism validation, precisely limited

LIBRARY-REPLACEMENT-TEST.json records actual loading of a separately modified QtCore copy with LD_BIND_NOW=1. A byte in the existing non-allocated .comment section was changed. All exported symbols, dynamic metadata/SONAME and loadable segments remained identical. The existing app returned Colosseum 1.1.9 through its pre-initialization --version path; loader diagnostics name the modified library. Original library and candidate are unchanged.

This supports the observed standard shared-library replacement mechanism and absence of byte-integrity/signing rejection. It is NOT full GUI functionality, a behavior-changing library build test, or qualification of every LGPL component. Interface compatibility remains essential; preserve accompanying library source/notices, replacement instructions and reverse-engineering-for-debugging rights. No promise of arbitrary incompatible replacements is made. FFL restrictions apply only to independent font software, never to the covered application's or LGPL libraries' rights.

## New source material and remaining delivery work

The new source tree preserves all 3,892 prior dependency-source/supplement entries unchanged. Its application archive has a new filename/hash and removes only the four Switzer OTFs; all other 5,626 members are unchanged. It includes authentic accompanying notices, full primary license texts, exact acquisition/restoration tooling, current build workflow, Qt configurations and selected shared-library instructions. The original font-containing application/source archives remain preserved separately and are not included in the new source delivery.

The new archive is 2,004,383,146 bytes, SHA256 22de73674922783e92a1dd313545a6d93c422f9ac729cca3b3bedbb9ab64a497. All 4,180 indexed members and the embedded index verified; five parts were streamed, hashed and byte-compared. Exact source content/index replay was independently verified; tar-byte equality across arbitrary receiver modes/umasks is not promised. The bounded recursive audit found no detected Switzer payloads, with documented inherited malformed/encrypted regression-fixture exceptions. These exceptions are not silently interpreted as proof of absent hidden bytes. The exact four-file application exclusion and unchanged dependency source identities are separate complete proofs. The unchanged original 2 GB packet must NOT be published under the new route. The new source packet likewise has NOT been uploaded or published by this work.

Remaining steps are owner adoption of this bounded interpretation and delivery mechanism, durable public hosting of the approved new source, ensuring notices actually accompany every distributed binary copy, and public unauthenticated download/hash verification. These are release/delivery steps, not an unexplained request for font provenance or mandatory ITF contact. This task does not authorize production publication.

If the owner insists on public redistribution of raw Switzer in source instead, that is a separate route requiring ITF clarification. The prepared short question and verified official contact in NARROW-DISTRIBUTION-ROUTE.md have not been sent. ITF can clarify its FFL permission, not waive unrelated GPL rightsholders' terms.

Safe evidence tar: 1,926,198 bytes; SHA256 1bf883cf3552db6d6757e9fbd6f7f326c5696835b1da2c544d3110ce5b71f825. Extract into an empty directory and verify its 324-file SHA256-INDEX.json using ../verify_packet.py. No raw fonts, installer or bulk source archive are included.
