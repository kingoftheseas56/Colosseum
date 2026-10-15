# Stremio Sync Design

**Status:** Approved — brainstorm and complete specification approved by Hemanth on 2026-09-19  
**Date:** 2026-09-19  
**Authorship:** [Scoped helper (Codex), product design]  
**Scope:** Colosseum Theatre only

## Authority and reference point

This document is the product contract for Colosseum's first external main-sync provider. It was designed inside the Brotherhood and does not adopt an unfinished Preflight arc as implementation authority.

[Harbor](https://github.com/harborstremio/harbor) is the proven behavioral reference for Stremio browser authentication and account API interaction. Colosseum adopts that user-facing login method, not Harbor's local-storage credential handling, progress heuristics, conflict guard, or application architecture.

## Current state

Colosseum already has its own profile and optional Neon account system, canonical Collection, Progress, History, and extension stores, durable account synchronization, Stremio-protocol addons, and Stremio-backed Theatre playback. It does not currently connect to a Stremio account or synchronize that account's library, progress, watched state, or addon collection.

The existing native sync imports deliberately suppress ordinary local `syncDirty` emission. Stremio-originated changes therefore need an explicit origin-aware relay into Neon; treating a provider import as an ordinary local edit would either strand the change on one device or create echo loops.

## Product promise

A person can connect one Stremio account to a Colosseum profile and trust Theatre library state, progress, watched state, History attribution, and Stremio addon configurations to stay aligned quietly, without replacing Colosseum identity, requiring a Neon account, risking playback, or deleting content unexpectedly.

## Locked product decisions

1. Stremio is a sync connection, not a Colosseum login method. Colosseum profiles and Neon remain the native identity system.
2. Each Colosseum profile may have exactly one external `mainSyncProvider`. Stremio is the only supported value in this arc.
3. Stremio sync works for both local-only profiles and Neon-backed profiles.
4. Each profile connects its own Stremio account. Library, progress, watched activity, History, addons, and credentials never mix between profiles.
5. One complete sync covers Theatre library additions, progress, watched state, Stremio-labelled History attribution, and the Stremio-compatible addon collection. There are no category toggles.
6. Library additions travel both ways. Library deletion never crosses automatically.
7. Library removal presents two explicit actions: **Remove from Colosseum** and **Remove from Colosseum & Stremio**.
8. The newest real viewing activity wins a progress conflict. A newer completed state beats an older partial position.
9. Colosseum retains its existing watched/completion rule. It imports Stremio completion state but does not adopt Harbor-specific playback thresholds.
10. First connection performs an automatic, deletion-free safe merge and shows a short result.
11. Disconnecting stops future sync and removes the device credential, while every already merged Colosseum library, progress, History, and addon entry remains.
12. Switching Stremio accounts removes the old device credential, preserves merged Colosseum data, then safely merges the replacement account without deletion.
13. Imported Stremio watched activity appears in History with a Stremio label and the latest known date. It contributes no playback duration, viewing hours, or other Colosseum Stats.
14. Sync is quiet by default. The panel shows recency and details; the Theatre icon shows a small attention badge only when action is required.
15. Configured Stremio addon URLs are device-private because they may contain tokens or personal settings. Neon never stores or transports them.
16. A new device learns only that the profile uses Stremio. It displays **Reconnect Stremio**, then retrieves the real addon collection from Stremio after authentication.
17. If a local-only profile later creates a Neon account, its merged library, progress, watched state, and History become the starting Neon state automatically. Stremio remains connected on that device; other devices require reconnection.
18. The official Stremio brand icon is always visible in Theatre between Search and Profile/Device. It is absent from Tankoban and Biblio and is not replaced with an imitation SVG.
19. Clicking the icon opens one focused Stremio Sync panel rather than a provider picker.
20. Authentication follows Harbor's proven external-browser and loopback-return method. Colosseum never asks for or receives the Stremio password.

## Primary experience

### First connection

The official Stremio icon is visible in the Theatre top bar. Clicking it opens the Stremio Sync panel in the **Not connected** state. **Connect Stremio** opens Stremio's official login page in the system browser. A securely correlated loopback callback returns the session to Colosseum automatically.

Colosseum validates the returned credential and account identity before saving it. The first sync then:

1. forms the union of Colosseum and Stremio library additions;
2. resolves progress and watched state using the newest real activity rule;
3. merges Stremio-compatible addon collections without deleting an addon on either side;
4. deduplicates addons by normalized transport URL; and
5. presents a short result such as items added, progress updated, watched states updated, and addons merged.

The first merge is automatic. It has no destructive preview, category picker, or advanced mode because it cannot delete data.

### Normal use

While Colosseum is running, sync occurs quietly on connection, profile activation, relevant local changes, return to the app when stale, and periodic best-effort refresh. **Sync now** is available in the panel. Exact debounce and refresh intervals are implementation details, but sync must be bounded and must not compete with playback.

Library additions, progress, watched changes, and Stremio-compatible addon install/remove/reorder actions propagate both ways after the initial baseline. Addon mutations are serialized as collection changes so one acknowledged collection state cannot race another. Colosseum-only extensions remain local.

Library and Continue Watching present one canonical Colosseum view without per-card provider badges. History alone retains Stremio attribution where provenance matters.

### Library removal

**Remove from Colosseum** changes only Colosseum. The next passive pull must not silently restore the item merely because it still exists in Stremio; the provider baseline records that the difference is intentional.

**Remove from Colosseum & Stremio** performs the explicit cross-service removal. If Stremio is temporarily unavailable, Colosseum completes its local removal, retains a durable pending remote action, and reports the pending state without blocking the user.

### Disconnect and account switching

**Disconnect** requires clear confirmation that sync will stop while merged data remains. It removes the device credential and future provider work, clears the active connection state, and keeps canonical Colosseum data and current local addon entries.

Connecting a different Stremio account is an explicit switch. Colosseum names the currently connected account before confirmation, removes its credential, preserves merged data, authenticates the replacement account, and performs another deletion-free safe merge.

## Panel, states, and feedback

The focused panel contains only:

- Connect or Reconnect;
- the connected Stremio account name;
- last successful sync time;
- Sync now;
- the latest first-merge or manual-sync result;
- Switch Stremio account when connected; and
- Disconnect.

Its user-facing states are **Not connected**, **Connecting**, **Syncing**, **Synced**, **Reconnect required**, and **Sync failed**. Errors state the consequence and next action in plain language; raw protocol or network errors stay in diagnostics.

The top-bar icon keeps the authentic Stremio artwork and is never recolored to encode status. A small surrounding badge indicates only actionable reconnect or persistent failure. Transient background work does not flash or interrupt. The icon and panel controls expose keyboard focus, accessible names, tooltips, logical tab order, and status announcements that do not repeatedly interrupt assistive technology.

## Identity, security, and privacy

- Authentication uses Stremio's official browser surface and a short-lived loopback listener bound to localhost.
- Each authorization attempt uses an unpredictable callback correlation value and accepts one valid response before closing. Timeouts, unsolicited callbacks, replay, and account-validation failure produce no saved credential.
- On Windows, the Stremio auth key is stored per Colosseum profile in Windows Credential Manager. It is never stored in QML, renderer storage, ordinary preferences, Neon, logs, diagnostics, or sync payloads.
- The panel may retain the non-secret Stremio account display identity needed to show who is connected.
- Changing Colosseum profiles changes the active provider connection atomically. No background result may be applied to a profile that is no longer its owner.
- Configured addon URLs remain device-local except when sent directly between that device and Stremio as part of the user's Stremio addon collection.

## Sync and data contract

### Canonical ownership

Colosseum's existing Collection, Progress, History, and extension stores remain canonical inside the app. Neon remains the cross-device authority for Colosseum media state when an account exists. Stremio is an external peer represented by an adapter, not a second Colosseum identity or a replacement database.

Neon may store only the non-secret marker that the profile's `mainSyncProvider` is `stremio` alongside canonical library/progress/history state. That marker is sufficient for another device to show **Reconnect Stremio**. Connection health stays device-local, and Neon must not store Stremio credentials or configured addon URLs.

### Origin-aware relay

Every provider mutation carries source, target profile, provider account identity, activity time, and a stable operation identity. Applying a Stremio change updates the canonical Colosseum store and, for Neon-backed profiles, schedules the resulting canonical state for Neon without re-emitting it back to Stremio as a new user edit. Neon changes arriving on another device may be sent to that device's connected Stremio account only when they represent a newer canonical user action and have not already been acknowledged by that provider.

Durable pending work survives process restart. Retry is bounded and idempotent. Provider failure never rolls back local persistence, blocks playback, or causes an unbounded foreground wait.

### Media identity and conflicts

The adapter must normalize Stremio media identifiers into Colosseum's canonical Theatre identity before merging. Unsupported or ambiguous identities fail as isolated records and remain visible in diagnostics; they cannot poison the full sync.

Progress resolution compares real activity times rather than request arrival order. The newest activity wins. Completion is not inherently stronger than newer partial playback: the locked special case is that a **newer** completion beats an **older** partial position. Equal or missing timestamps use a deterministic stable tie-break and never oscillate between services.

### Addon collection

The first merge preserves Stremio's existing order, deduplicates by normalized transport URL, and appends Colosseum-installed Stremio-compatible addons that are absent from Stremio in their Colosseum order. No addon is deleted during that merge. After baseline, explicit install, removal, and reorder mutations synchronize as part of the single addon collection.

Colosseum-native extensions or extensions outside Stremio's compatible collection format never enter Stremio sync.

## Recovery and edge cases

- **Offline or temporary Stremio failure:** local work completes, pending provider work persists, quiet retry continues, and the panel reports the last success.
- **Expired or rejected credential:** sync pauses for that profile and the icon gains an attention badge. **Reconnect Stremio** restarts official browser authentication.
- **New Colosseum device:** Neon restores canonical media state and the linked marker, but no secret or addon URLs. Reconnection retrieves Stremio's real addon collection.
- **Profile switch during sync:** in-flight results are profile-bound; stale results cannot cross into the newly active profile.
- **Colosseum account creation after local use:** existing local merged state seeds Neon without disconnecting Stremio on the current device.
- **Partial record failure:** valid records continue; failed records retry or remain diagnostic rather than failing the entire collection.
- **App exit:** durable work remains pending for the next launch. This arc does not introduce an always-running operating-system sync daemon.
- **Remote library removal:** it is observed but not copied into Colosseum automatically.

## Acceptance criteria

1. A local-only profile and a Neon-backed profile can each connect a Stremio account without entering a Stremio password in Colosseum.
2. Two Colosseum profiles can connect different Stremio accounts on one device without credential or data leakage.
3. First connection combines both libraries and compatible addon collections, selects the newest progress/watched state, deletes nothing, and shows a concise result.
4. Subsequent library additions, progress changes, watched changes, and addon install/remove/reorder changes reach the opposite service and settle without echo loops.
5. A newer completed state replaces an older partial position; a newer partial position is not incorrectly overwritten by an older completion.
6. Passive library removal on either service never deletes the other service's copy. The explicit dual-removal action reaches both, including after an offline retry.
7. Imported Stremio activity appears in History with source and date but leaves Colosseum viewing-duration statistics unchanged.
8. Playback and local persistence remain functional while Stremio is offline, slow, unauthenticated, or returning malformed individual records.
9. A new device shows **Reconnect Stremio**, contains no transferred Stremio secret or configured addon URL, and retrieves addons only after successful reconnection.
10. Disconnecting or switching accounts removes the old device credential while preserving merged Colosseum data.
11. The official Stremio icon appears in the required Theatre top-bar position, never appears in Tankoban or Biblio, and exposes usable keyboard and assistive-technology behavior.
12. The UI exposes one complete sync and no provider chooser, category toggles, or unsupported provider affordance.

## Constraints

- Avoid overengineering and scope creep; build the smallest complete Stremio integration against Colosseum's existing native stores and Neon system.
- Provider work is lower priority than playback and local durability.
- Credentials are device-local and secure.
- Cross-service library deletion is explicit only.
- The official Stremio asset must be used according to its brand presentation; status treatment surrounds rather than alters it.
- This is a Brotherhood-owned Colosseum arc. Historical Preflight artifacts are reference evidence only.

## Non-goals and deferred work

- Trakt integration, including ratings and reviews.
- Nuvio integration and the future provider-selection UI.
- Simultaneous external main-sync providers.
- A general-purpose multi-provider framework or plugin SDK.
- Stremio email/password entry inside Colosseum.
- Stremio subtitle preferences, player preferences, recommendations, social data, or account settings.
- Neon transport of Stremio secrets or configured addon URLs.
- Tankoban or Biblio integration.
- An always-running background daemon when Colosseum is closed.

The future Nuvio arc may add a selector for the single `mainSyncProvider` slot. Switching providers must preserve merged Colosseum data, disconnect the old provider, and safely merge the new one. No Nuvio pixels or generalized provider machinery are justified in this Stremio arc.

## Discarded approaches

- **Copy Harbor wholesale:** rejected because Harbor's renderer credential storage, lightweight conflict guard, and absence of Colosseum/Neon ownership do not meet Colosseum's security or cross-device model.
- **Use Stremio as login:** rejected because it would collapse external sync into Colosseum identity and break account-optional local profiles.
- **Build a Stremio/Nuvio chooser now:** rejected because only Stremio is being delivered and speculative UI would create dead controls and framework scope.
- **Automatically mirror library deletions:** rejected because accidental or stale remote state could destroy a user's canonical Colosseum library.
- **Store addon configurations in Neon:** rejected because configured transport URLs may contain secrets or personal settings.

## Decision ledger

**Locked:** All behavior described in this specification, including single-provider exclusivity, safe two-way sync, first-merge semantics, conflict behavior, addons, profile isolation, account-optional use, Theatre placement, browser authentication, secure device-local credentials, quiet recovery, History treatment, disconnect, and account switching.

**Constraints:** Colosseum/Neon remains canonical; no automatic cross-service library deletion; playback and local durability cannot depend on provider health; credentials and addon configurations do not traverse Neon; Theatre only; official Stremio branding.

**Deferred:** Trakt, Nuvio, ratings/reviews, recommendations, provider chooser, simultaneous providers, generalized provider framework, and non-Theatre integration.

**Open:** None.
