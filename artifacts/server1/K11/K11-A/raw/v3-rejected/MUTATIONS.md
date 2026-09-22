# K11-v3 behavioral mutations

Producer: `[Agent 4 (Codex/Sol subagent), K11-v3 producer]`

Each mutation was applied alone, rebuilt, run, captured under `raw/`, and reversed before final build.

1. Availability keyed by virtual instead of verification index — missing exact virtual blocks.
2. Request/Cancel bypassed verification-wire mapping — exact wire-coordinate assertion.
3. Throwing hook still scheduled continuation — zero-construction assertion.
4. Incomplete verification falsely marked committed — precommit visibility assertion.
5. Post-ScopedReady map guard removed — reentrant remove assertion.
6. Repeat-timer cancel omitted — exact cancellation count.
7. Persistent upload bypassed `isCommitted` — precommit abort assertion.
8. Block generation check omitted — stale ownership assertion.
9. Retry reused request id 1 — fresh request-id assertion.
10. Corruption returned before group reset — group retry assertion.
11. SchedulerActionContract output discarded — FileReader-to-K10 action assertion.

An early-notify-only experiment was not counted because committed-only source checks made it inert; its logs were removed.
