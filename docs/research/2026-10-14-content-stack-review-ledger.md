# Independent free-stack comparison and receiving decisions

2026-10-14. Parent task: complete a portable catalogue/Universe/Tribute migration plan while R2 registration is pending. Six combinations received separate Preflight Spawn tasks. Two Sol 6.1 agents separately inspected native content and projection semantics; one also supplied a supplemental Turso review. No new accounts, subscriptions, production deployment or native migration were performed for this comparison.

Canonical result: [portable migration plan](2026-10-14-content-service-migration-plan.md). The next proof should compare Turso Free/libSQL against a reduced D1 projection. Object storage remains independently selectable; R2 is optional and cannot be represented as a guaranteed zero-spend product. Existing D1 staging remains intact.

## Receiving status

| Task | Combination | Report and reviewer verdict | Parent disposition |
| --- | --- | --- | --- |
| `content-options-01-d1-r2` | Workers + D1 + R2 | [Report](content-stack-reviews/REVIEW-01-d1-r2.md): conditional; reject a guaranteed-zero-bill claim | Accepted as research with qualifications below; not production approval |
| `content-options-02-d1-static` | Workers + D1 + Static Assets | [Report](content-stack-reviews/REVIEW-02-d1-static.md): bridge only; reject unqualified full-service replacement | Accepted; file count and D1 writes are material, but SQL-backed details remain an alternative to per-title files |
| `content-options-03-neon-r2` | Workers + Neon + R2 | [Report](content-stack-reviews/REVIEW-03-neon-r2.md): conditional staging; reject guaranteed hard-$0 | Accepted; compute uptime and 5 GB public transfer make it lower priority |
| `content-options-04-supabase` | Supabase Free DB/Storage/Functions | [Report](content-stack-reviews/REVIEW-04-supabase.md): small pilot only | Accepted; size/egress/function and inactivity constraints make it lower priority |
| `content-options-05-r2-indexes` | Workers + R2 indexes, no SQL | [Report](content-stack-reviews/REVIEW-05-r2-indexes.md): selective/static content plausible; dynamic parity unproved | Accepted; reject as default search backend unless worst-case query proof changes the result |
| `content-options-06-turso-r2` | Workers + Turso Free, R2 optional | [Report](content-stack-reviews/REVIEW-06-turso-r2.md): conditional; prefers Turso documents without mandatory R2 initially | Accepted with accounting/publication corrections below; agrees with [supplemental Sol review](2026-10-14-turso-free-stack-review.md) on next qualification |

“Accepted” means read and assessed as bounded research, not acceptance of every suggestion or a tested implementation. Reports distinguish official terms, supplied baseline facts, calculations and proposals. No review establishes import speed, geographic latency, production auth or native parity. Copies preserve the received reports; parent corrections below govern the final plan.

## Findings that changed the plan

1. **Do not continue the full D1 copy by default.** Its 3,177,359 estimated source/index/ledger writes imply 53 capped windows at 60,000/day. Object storage does not remove searchable rows or index costs. The native projection note measures alternative shapes and preserves semantics.
2. **Give Turso a real comparison.** Its current monthly free write pool is materially larger than the D1 daily allowance. A SQL-backed bounded JSON document adapter plus URL/Static Assets artwork could operate before R2 registration. Import accounting, HTTP transfer terms, quota blocking and the explicitly selected libSQL engine need qualification.
3. **No one-file-per-title Static Assets design.** The measured 125,429 rich-root documents exceed the 20,000-file free limit. Static Assets remains useful for the small artwork/Universe collection, while database documents or bounded internal shards can implement selective detail reads.
4. **No automatic speed or scale claims for R2.** Cache hits reduce origin work, while authenticated Worker calls still consume the daily request allowance. R2-only search shifts complexity into object fan-out and a 10 ms CPU budget.
5. **Free capacity is workload-dependent.** All Worker-fronted candidates fail 5,000 DAU at 30 dynamic calls/day before considering origin costs. Supabase/Neon add transfer/compute limits. Client request coalescing and measured query costs are required, not assumed.
6. **Content publishing can be release-independent on every viable backend.** Versioned declarative documents, renderer compatibility, prepare-then-activate publication, last-good caches and rollback are API contracts. Backend switching changes adapters and publication tooling, not personal identity/progress or the app's semantic API.

## Corrections and qualifications applied by the parent

- Several reviewers use 134,295 title-like rows as an illustrative subset. It excludes 1,134 curated series and does not deduplicate Tankoban. The source audit gives 135,429 root rows, or 125,429 rich-root documents after attaching the 10,000 matched Tankoban records to MAL manga. Do not use the subset as completeness proof.
- “Reject at 5,000 DAU” applies to the supplied 30-dynamic-call workload, not every conceivable deployment at that user count. A different measured workload can change the result; no free unlimited-growth claim follows.
- “Reject as full-scale durable backend” is a reviewer judgment under unknown workload and parity, not proof that a vendor cannot serve a smaller qualified product. The final plan ranks candidates and defines gates rather than declaring untested alternatives impossible.
- D1's 50-byte LIKE/GLOB pattern limit is a specific native search compatibility risk. A compliant alternative query/index must preserve semantics for longer/multibyte text. Prefix-only search is not equivalent to the current substring behavior.
- The Neon report's always-awake illustration uses approximately 182.5 CU-hours (a 730-hour average month). For the plan's explicit 30-day month, 0.25 CU times 720 hours is 180 CU-hours; both exceed 100.
- Supabase cached Storage egress is separate from uncached API egress. Turso embedded-sync transfer is separate from ordinary SQL-over-HTTP responses; neither allowance should be reassigned to another meter.
- Direct public Static Assets are appropriate only for intentionally public content/art. Protected catalogue responses must authenticate before cache access. Anonymous native app leases cannot prove an unmodified genuine app; CORS or an embedded key does not solve this.
- R2 budgets/alerts do not constitute a provider hard spending cap. The final plan specifies bounded read fan-out and operator publication ceilings as proposed controls, with account-wide reserves and tests; those controls are not already deployed.
- Operator-to-server bulk import is allowed as a migration option on a qualified backend. The user's prohibition concerns app-user DB/content-pack downloads. No import is assumed free of provider accounting.
- The D1 + R2 report was initially observed while incomplete. A follow-up requested completion and the final source register/workload/security sections and corrected root counts were reviewed. The final 17,597-byte report fits the requested 18 KiB bound.
- The Turso Spawn report says its index-write multiplier is unproved. The current official pricing page's structured FAQ explicitly says affected indexes add writes; the parent and Sol reviewer verified that HTML. **Index writes count; the exact D1-derived multiplier and binary-import accounting still require measurement.** The 11.33% base-row example is a lower-bound sensitivity, not an import budget.
- An earlier Turso draft mentioned temporary split views; the final report removes that phrase. The plan explicitly requires preparing all objects/query data, verifying them, then activating one pointer. Old and new complete revisions may coexist for in-flight readers, but a single response must not mix them.
- The Turso report's content-update statement describes the target after migration. Current QML declarations and process-lifetime payload caches still require extraction and refresh integration; independent updates are not already implemented.
- The Turso worker shortened its initial draft to a final 17,583 bytes, within the requested 18 KiB target. The changed sections were re-reviewed before preserving the final snapshot. All six reports are now received and reviewed.

## Dispatch provenance and report integrity

Mission directory on the originating Windows host: `C:\Users\Suprabha\Desktop\Loop-Missions\content-options-20261014`. Each first-round prompt has its exact `TASK-01` through `TASK-06` filename and one exact reply path. The original five were confirmed SENT in retained tabs 2–6. Report files, not SENT receipts, are completion evidence.

The sixth dispatch initially stopped before allocation with `all-six-tabs-assigned`. Its normalized prompt hash was `c83fb0589664861ba4c2648d2d1406f05e65a3d30816fdb110348acdfef79413`; checking the shared receipt ledger found no receipt and no report. The newer source-host helper then confirmed SENT for that exact previously unsent prompt. No already-sent first-round prompt was resent and no occupied tab was closed or replaced. A targeted completion follow-up used the original D1 + R2 retained tab.

Report SHA-256 hashes are recorded below after copying exact received UTF-8/LF bytes. These identify the reviewed snapshots; subsequent worker changes require reassessment. When checking a checkout that converts line endings, normalize CRLF to LF before hashing, or hash the Git blob directly.

| Report | SHA-256 |
| --- | --- |
| `REVIEW-01-d1-r2.md` | `5ece09706510beec7312dda1ce0c41dfb4ec42e23347c6506a47d3714375b7ab` |
| `REVIEW-02-d1-static.md` | `935e33b7e0e029bceb95df3f3f6fffea9a67e18635c988dea6ffebb7046b2ffd` |
| `REVIEW-03-neon-r2.md` | `3bc10d28f2d4f26e3c294edb4f5374e4321382636f95c69ed2be2a6f547c339c` |
| `REVIEW-04-supabase.md` | `78b22aae1a200762b8815a7eac4b62c946cab8b967274b2b35ce38e880e71d3e` |
| `REVIEW-05-r2-indexes.md` | `2e8df38c299d7f018fa7a81b89794046d0cfb663f1581c72b4ce8c096632229e` |
| `REVIEW-06-turso-r2.md` | `8dc88e45a067d5ddb48014d306367b58af9e42dddbfd7656d5cbf45a0db1d48c` |

The source inventory and architecture draft also received a read-only Sol review. It found no blocking scope/portability issue and requested explicit Turso quota behavior, staging evidence links, Unicode/null/tie parity cases and complete R2 operation accounting. Those additions are incorporated in the canonical plan. No build/runtime result is inferred from a document review.
