# Preflight Architect: Brotherhood Lanista Workflow Skills Creation Guide

## Recommendation

Use three Brotherhood skills only:

```text
Brotherhood Brainstorming
→ approved specification
→ Brotherhood Writing Plans
→ Brotherhood Executing Plans
```

Do not add a separate specification skill. Brainstorming already owns the reviewed specification.

Do not add a separate Lanista skill. Create one shared `colosseum-lanista-verification.md` reference used by Writing Plans and Executing Plans.

## Brotherhood Writing Plans

Create `brotherhood-writing-plans`.

Every user-visible slice must define both:

1. the implementation outcome; and
2. the running-app evidence required to prove it.

Required slice fields:

```markdown
### Slice N: <Outcome>
Purpose:
Dependencies:
Implementation guidance:
Behavior to preserve:

Baseline:
Focused tests:
Lanista actions:
Completion signal:
State / events / probes:
Visual evidence:
Regression paths:
Evidence artifacts:

Bridge status: available / bridge blocked / not applicable
Completion criterion:
```

A user-visible slice is not execution-ready when it says only “test manually,” “verify the UI,” “ensure it works,” or “take a screenshot.”

When Lanista lacks a required capability, mark the slice **Bridge blocked** and order the smallest safe bridge prerequisite. Never invent commands, probes, or semantic waits.

## Brotherhood Executing Plans

Create `brotherhood-executing-plans`.

Required loop:

```text
confirm repository and plan revision
→ reproduce baseline
→ preserve baseline evidence
→ implement smallest approved slice
→ run focused tests
→ replay planned Lanista scenario
→ inspect state, events, probes, and pixels
→ exercise named regressions
→ preserve evidence manifest
→ report supported status
```

The agent must not silently redesign the plan, replace verification with easier checks, use unit tests as runtime proof, add arbitrary sleeps, drive the daily app by default, or claim “fixed” without current evidence.

Allowed statuses:

- Implemented, verification pending
- Bridge blocked
- Verification failed
- Plan contradicted
- Test-reported
- Runtime-validated

Only **Runtime-validated** closes a user-visible slice without qualification.

## Shared Lanista reference

Create `colosseum-lanista-verification.md`.

It must classify capabilities as9

- Available now
- Planned
- Unavailable
- Human-only

Document current gates, MCP tools, commands, scenario runner, screenshots, selectors, waits, events, probes, daily-app restrictions, artifacts, failures, and timeouts.

Agents must never use a planned bridge as if it already exists.

## Existing skill handling

- Keep `brotherhood-brainstorming` authoritative.
- Resolve or retire the overlapping `brotherhood-superpowers` dispatcher.
- Keep `brotherhood-ui-audit` independent.
- Do not create `brotherhood-specification`, `brotherhood-lanista`, or a separate review skill in version one.

## Pressure tests

Ship both new skills with the Brotherhood red/green pressure-test pattern.

Writing Plans cases:

- deadline pressure;
- missing bridge;
- internal refactor;
- daily-app risk;
- screenshot-only temptation.

Executing Plans cases:

- unit tests pass but runtime evidence is missing;
- scenario fails;
- plan contradiction;
- bridge blocked;
- aesthetic judgment still requires Hemanth’s eyes.

## Pilot

Use the Biblio blank and low-quality cover issue first.

Require one blank cover, one low-quality cover, one healthy analogue, cold and warm cache, delegate recycling, navigation away and back, fallback behavior, and per-card source/network/cache/decode/QML/pixel evidence.

## Decision gates

1. Three core skills only.
2. Brainstorming owns the specification.
3. Writing Plans owns implementation slices and verification design.
4. Executing Plans owns implementation, verification, evidence, and status.
5. Lanista is a shared reference.
6. Missing bridge capability is an explicit blocker.
7. User-visible completion requires running-app evidence.
8. Daily app and live user data are not disposable fixtures.
9. Pressure tests ship with both new skills.
10. Genuine aesthetic judgment remains a human gate.

## First action

Inspect the target branch’s actual skill root and discovery conventions, then confirm how both new skills can reliably retrieve the shared Lanista reference before freezing paths.
