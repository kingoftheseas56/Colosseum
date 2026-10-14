# Tankoban staging upload and verification — 2026-10-14

## Result

Tankoban staging is complete and ready: **10,000 series and 28,839 volume rows**.
The remaining 3,335 volumes were uploaded in 105 deterministic, resumable chunks.
Every hosted rowid, key and source column was independently compared against the
frozen local source using ordered UTF-8 page SHA256 digests before the volumes
readiness flag was enabled. All 105 new chunk receipts also match their source
payload hashes, day and reserved costs. Source files were opened read-only and
the original hash was unchanged.

Source SHA256:
`04d485c6a559fdc7b6d1d651a622befd77af8bf93b2c7bdbcf7ebbd7bdd7e5a1`.

Staging origin:
`https://colosseum-content-api-staging.colosseum-watchparty-relay.workers.dev`.

No Worker deployment, token rotation, billing-plan change or native candidate
change was needed. This supersedes the earlier plan to wait until the next UTC
day for the remaining Tankoban upload. The broader compact projection remains
unimported; its full publisher and consumer qualification are separate work.
The user waived a separate Astra review; reproducible receiving checks replace
that model-specific checkpoint, not the remaining data and application gates.

## Free quota and temporary allowance

The current account-wide UTC-day analytics initially reported **28,091 reads /
57,319 writes**. Cloudflare documents D1 Free limits of 5 million reads and
100,000 writes per UTC day, with quota errors rather than paid overage.
[Cloudflare D1 pricing](https://developers.cloudflare.com/d1/platform/pricing/).

The project's conservative import guard was 60,000 reserved writes, with 58,978
already reserved. The exact remaining chunks modeled 7,720 more units. To finish
within the verified provider headroom, a temporary trigger allowed **only the
105 derived chunk IDs on UTC 2026-10-14**, capped at 67,000 reserved units.
Every other chunk and day retained the 60,000 guard. Five local SQLite guard
checks covered the exact allowance, other chunks, overrun, other days and
restoration. The original trigger SQL was restored immediately after upload
and independently read back. No permanent migration or guard change was made.

The ledger now records **66,698 reserved writes** for UTC 2026-10-14. That is
an estimate, not provider metering. Post-upload account-wide analytics reported
**106,838 reads / 64,412 writes** across all database bins for the exact UTC day.
The **7,093-write delta** includes the upload, trigger changes and readiness
update. Existing other database bins were unchanged. Provider analytics lag
remains unknown; the snapshot is not a promise of future spare capacity.

The full content readback used 38,840 metered reads plus 9,000 diagnostic reads
when the connector's displayed large result arrays proved truncated. Final
verification hashes were computed over full rows inside the connector before
output formatting, and compared with source hashes locally; truncated display
arrays were not accepted as completeness evidence. Receipt checks used another
636 metered reads. Schema/status/completion and live-route reads are included
in account analytics rather than falsely described as free.

## Verification

The independent full readback verified 40 pages, including the empty terminal
series page. Counts, rowids, identities, volume counts/count bases, exact number
strings, cover URLs and names match the original source.

Digest chain over ordered page SHA256 values:

- Series: `8df22706556f45c227542e8ef07095c6236924bb65e8234d8ea059e5691a9263`
- Volumes: `02af063eedcd14fd8e676a1cd5844574120473dfe68e7d07bc12c4d24c6a9bf5`
- All 105 new receipt records: `f5e10135684eb48c63966835342fc05baebdcd4ee0bca05fae86aee6ab22c3ec`

A receipt-only diagnostic initially differed on one chunk containing Unicode
apostrophes. The temporary helper had read UTF-8 JSON with Windows' default
encoding. A local Node reproduction matched the actual Worker hash; explicit
UTF-8 made all Python receipt hashes match too. No uploaded data was changed or
rewritten in response to this verification-tool error.

**12 live HTTPS checks passed** using a newly issued guest lease, without admin
credentials in guest requests or reports:

- Session issuance: 200, correct audience/scope/source hash, 300-second lease.
- Exact source-oracle responses for MAL IDs 1, 2, 13, 134233, 3 and 2147483646.
  These cover populated, zero-volume, newly uploaded, synthesized and unknown
  series; metadata, full ordered volumes and all returned fields match.
- MAL 13: volumeCount 113 and 113 ordered volume entries.
- Missing/invalid lease: 401 `UNAUTHORIZED`.
- A guest lease cannot read generic catalogue routes: 401 `UNAUTHORIZED`.
- Out-of-range MAL ID: 400 `INVALID_MAL_ID`.
- Wrong guest request body: 400 `INVALID_GUEST_REQUEST`.

Final authenticated status independently confirms both Tankoban tables have
matching source hashes, expected/imported counts and `ready=1`.

Private local receipt identities (receipts and authentication files stay out
of Git):

| Receipt | SHA256 |
|---|---|
| Full readback | `17f7ac20257fd5cae2d75c235cb17dc039a9d4a853d5a21e63604998ab7380a3` |
| New chunk receipts | `c26ef5ce4d1c5b528c2e67a3302a448758e704a18fa808bd963db5d1aafeea69` |
| Live guest/API checks | `eeacd684369845754472f060d85ed92ab367732c95cc9748c6864203044c9663` |
| Final status | `c0d0e7ba70d64395dea2bdc67fa059429c4d294df6391efb1719e5534fe9baa8` |
| UTC account analytics | `a86e8e7b658abfe125ea1f9211fc9cc465bf85526c10c7202c2a22860ba85743` |

## Next proof

This establishes hosted Tankoban integrity and live guest/Worker behavior. It
does not establish native networking, Qt GUI use, durable offline caching,
asset image retrieval, lease expiry, Linux packaging, Windows upgrade preservation
or release readiness. The candidate remains pinned at
`dfb4b8c90926ec0862a5612c3cc59faca1f17c51` on
`codex/d1-119-no-account`; Git remote advertisement matches that pin.

The Dots prompt beside this note requests a matching no-account Linux build and
real app evidence. Neon, Biblio, Feria and Vinyl remain outside the assignment.
No final signing, feed activation, Windows installer or production release was
performed. Dots has not been dispatched by this task.
