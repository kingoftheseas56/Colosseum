# Colosseum Player F11 — Up Next during credits (design)

**Date:** 2026-07-18 · **Owner:** Agent 4 (player lane), queued AFTER F9 seek thumbnails · **Status:** ratified by Hemanth (Jellyfin UI inheritance pass, 2026-07-18); his eyes-on is the acceptance gate.

## What it is (plain sentence)
The Up Next card we already show after an episode ends now appears when the **credits begin**, so you glide into the next episode without ever watching the file run out.

## Why
From the 2026-07-18 Jellyfin UI inspection (Agent 0 + Hemanth). Both halves of this already exist in our player — the audit's job was finding the gap between them:
- **The card exists** (Harbor parity): `startUpNextCountdown()` / `cancelUpNext()` / `confirmUpNext()` + the countdown Timer + the card chrome, fired today only on end-of-file (`onEndFile reason === "eof"`, PlayerPage ~2538).
- **Credits detection exists** (Feature 4): `skipSegments` carries chapter- and AniSkip-derived ranges with `kind === "credits"`.
The one thing missing is the wire between them. Jellyfin/Netflix binge feel = the card rides OVER the credits; ours waits for black. This is a trigger-timing change, not a new surface.

## Design (smallest thing that works)

**Trigger (the new wire).** On position entering a `credits` skip segment (same `SkipSegments.activeSegment` walk the skip pill uses) AND `hasAdjacentEpisode("next")` AND not dismissed for this episode → show the existing card in a new **passive mode: no ticking countdown**. The credits themselves are the countdown. Card shows next episode identity (existing `upNextTitle()` / `upNextArt()`), a Play-now action (`confirmUpNext()`), and a dismiss.

**EOF becomes the deadline, not the announcement.**
- Card still up (never dismissed) when EOF hits → advance immediately; the viewer had the whole credits as warning.
- Dismissed during credits → EOF does NOT re-pop the card and does NOT auto-advance; dismissal means "I'm stopping here" (mirror `dismissedSkipKey`: a per-episode `upNextDismissedKey`, reset in the same places skip state resets — `resetSkipSegments()` / new-file load).
- No credits segment known for this file → today's behavior unchanged: EOF → 10s countdown card. This is the fallback lane, not a regression.

**One prompt, never two.** While the Up Next card is visible, suppress the "Skip Credits" pill — the card's Play-now IS the skip when a next episode exists. (Two stacked prompts pointing at the same jump reads broken.) `autoSkipCredits` ON keeps its meaning: it jumps to EOF, which lands in the card-still-up → advance-immediately rule — the setting becomes "instant next episode", which is exactly what that user asked for.

**Chrome independence.** The card shows even while the HUD is asleep (that is its point — you are watching credits, not reaching for controls). Keep the existing card visuals; body gets the house click-swallower MouseArea if it lacks one. No new fonts, no new colors: existing glass + the one gold accent already in the card.

**Movies / no-next.** `hasAdjacentEpisode("next")` already gates everything — movies and season-enders never see the card (unchanged).

## Out of scope
Series-end "more like this" card · changing the 10s fallback countdown length · any Theatre/library-side "Next Up" row (separate thread from the same inspection, not yet ratified) · thumbnails inside the card beyond the existing `upNextArt()`.

## Testing
Pure logic (enters-credits edge detection, dismiss-key lifecycle, EOF disposition table: {card-up, dismissed, no-segment} × {advance, stop, fallback-countdown}) → headless harness alongside the SkipSegments tests. Wiring → grep contract (trigger present in the position path, skip-pill suppression bound to card visibility, dismissal checked at EOF). Feel — card over real credits on an episode with AniSkip data, dismiss-then-EOF stops clean — is Hemanth's eyes-on.
