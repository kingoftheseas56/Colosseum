# Collections sharing implementation and acceptance

Baseline: `27a75ea896345f6ccedf82d7ef382673b5fecd6d`.
Working branch: `dots/collections-sharing-20261030`.

## Scope

Extend the existing profile-owned NamedCollectionsStore, account-service authentication/PostgreSQL, and Qt/QML Collections destination. Add only the browser receiving surface explicitly requested. Local collections and gift drafts remain offline/account-free. Publishing requires existing Colosseum account authentication. Production adoption, migrations and deployment are excluded.

## Engineering choices

- Existing account service owns publications using transactional PostgreSQL persistence and its existing authenticated sessions.
- Anonymous viewer assets are served by that service, avoiding a new vendor or cross-origin credentials.
- Wire schema version 1 is an explicit allowlist. Native reopen payloads, local paths, provider credentials and media are never exported.
- Explicit optimistic-revision ordinary publication updates; immutable gift editions; idempotent operation IDs and owner-only revocation.
- One bounded embedded PNG/JPEG presentation cover makes permitted asset retention atomic with the gift, without remote image downloads. Midnight, Paper and Sea presets are presentation choices, not new collection types.
- Kept gift editions remain immutable alongside editable independent copies. Copies reset personal notes, dedication, signature and decorations.
- Original source namespace, identity and edition survive import; only exact available recipient routes may open. Missing references stay visible.

## Work sequence

1. Agree wire/native contracts and adversarial cases; establish baseline tests.
2. Implement server validation, persistence, owner and public endpoints with tests.
3. Extend native store projection, presentation, atomic gift retention and independent copying with tests.
4. Wire existing account transport and profile transitions to native publishing/import controls.
5. Assemble responsive anonymous viewer and native flows.
6. Build matching Qt candidate, run focused/regression tests and real Linux/browser journeys.
7. Review exact diff and evidence, commit working branch, deliver concise record after authorized publication. No production merge/release.

## Required assembled journeys

- Owner publishes mixed media; signed-out desktop and phone-sized browser explore it.
- Private edits do not alter a link; Publish changes updates ordinary publication; gift edition remains fixed.
- Passphrase success/failure, cross-account rejection, revocation, malformed/oversized and hostile text/image payloads.
- Recipient keeps gift, restarts offline with retained assets, creates/edits independent copy; sender, kept edition and per-world library/progress unchanged.
- Missing sources/editions, profile changes, interrupted persistence/import and repeated requests recover without partial saves or accidental duplicates.

Record actual commands, exact final source/executable hashes, OS/architecture and Qt versions. Unit, service integration, app runtime and browser runtime are distinct. Windows/Android are NOT RUN unless actually exercised. A partial check is never an end-to-end pass.
