# Idea — Race the sources at press-play (and the ladder above it)

- **Date:** 2026-08-07 · **Origin:** Hemanth, during the Tankorent 2.0 Phase-0 planning session
- **Status:** IDEA — recorded, not scoped, not scheduled. No plan exists.
- **Related arc:** TANKORENT_2_CHALLENGER
  (`docs/superpowers/specs/2026-08-02-tankorent-2-challenger-engine-design.md`,
  `docs/superpowers/plans/2026-08-07-tankorent-2-phase0-rosetta-dig.md`)

## The idea, in Hemanth's words

> Race the candidates at press-play. Start the top two or three Torrentio results at once,
> measure real throughput for about five seconds, keep the winner and drop the rest. This
> kills the worst failure in the whole experience — picking a torrent whose seed count was
> lying, then sitting on a spinner for thirty seconds before finding out. No cross-torrent
> chunk math needed, because you're picking a winner, not splicing. And if the race happens
> to reveal two candidates hold the identical file, then you promote them to a shared pool
> and pull from both.

And the ordering he proposed, cheapest and highest-payoff first:

> merge duplicate swarms → turn on peer-exchange so we see the whole swarm → race slow chunks
> across peers → race torrents at startup → true multi-source only for verified-identical files

## Why it is worth keeping

It targets the single worst felt failure in Theatre: choosing a source on a **claimed** seeder
count, then discovering thirty seconds into a spinner that the claim was stale or inflated.
Racing replaces a claim with a measurement, and it does so in the only currency that matters —
bytes actually arriving. The user-facing promise is small and concrete: press play, and within
a few seconds you are on the source that is genuinely fastest right now, not the one that
advertised best.

It also composes with the rest of the arc instead of competing with it. Peer count is the
floor: no source-selection cleverness helps if the engine can only see a handful of peers in
the room. Racing sits **above** the engine; peer-exchange and reachability sit **inside** it.

## The structural point worth remembering

**Rungs 1, 4 and 5 of the ladder do not depend on the Tankorent 2.0 verdict.**

Merging duplicate swarms, racing candidates at press-play, and verified-identical multi-source
are all decisions about *which torrent(s) we hand the engine* — they live in source selection,
above whichever engine is running. They remain buildable on Stremio even if Phase 0 returns
STOP and we never build a challenger.

Rungs 2 and 3 — peer-exchange and racing slow chunks across peers — are engine-internal, and
their fate is tied to the arc: if we stay on Stremio, we can only have them if Stremio already
has them.

This matters for sequencing. A STOP verdict does not empty the backlog; it just moves the work
to the rungs that sit above the engine.

## Claims to verify before any of this is planned

Recorded as open questions, deliberately not as findings:

1. **Do Torrentio's candidate lists actually contain byte-identical files often enough to
   matter?** The identical-file case (an episode also present inside a season pack) is the only
   one where true multi-source is legitimate, because each range still validates against its
   own torrent's checksums. Whether it is common in *our* results is a measurement over real
   Torrentio responses, not an assumption. Cheap to check; nobody has.
2. **Does a five-second sample actually predict the next ninety minutes?** A race measures the
   opening seconds, which is when swarms are still warming and connections still forming. The
   winner at five seconds may not be the winner at five minutes. Needs measurement before the
   window length is chosen.
3. **What does the parallel race cost?** Starting three torrents means three sets of
   connections and three partial downloads, two of which are discarded. On a home line that is
   real bandwidth spent during exactly the seconds the film is trying to start. The race could
   plausibly make cold-open *worse* than picking one source well. This is the strongest
   argument against the idea and must be measured, not reasoned about.
4. **Is per-peer chunk racing already present?** Endgame-style racing of overdue blocks across
   multiple peers is long-standing torrent-client behavior. Before building rung 3, confirm
   whether the engine in question already does it — Phase 0's Slice 2/4 work can answer this
   for Stremio as a side effect.

## One correction to carry forward

The Phase-0 conversation produced a strong signal that Stremio lacks peer-exchange: the string
`ut_pex` does not appear in the bundle, while `ut_metadata` does. **That is a signal, not a
finding.** Absence of a string in a packed bundle is weaker evidence than presence, and the
capability could be present under another name or via a bundled dependency. Any writing that
states "Stremio has no PEX" as established fact is running ahead of the evidence. Slice 4 of
the Phase-0 plan settles it by live observation — with an explicit negative control on peer
attribution, precisely because a broken attribution would make the conclusion vacuous.

## Cross-reference into Phase 0

The Phase-0 lever inventory (Slice 3) should score two additional levers it would otherwise
miss, both drawn from this ladder:

- **swarm merge** — does the engine pool peers across torrents that share an identical file?
- **startup source race** — does the engine (or its client) measure candidates before
  committing to one?

Both are detectable, which is the inventory's entry requirement. Neither expands Phase 0's
scope: they are two rows in a table that is already being written.

## Not scheduled

No plan, no owner, no date. This document exists so the idea is not lost in a chat log, and so
the next brother who reaches for "why don't we just stream several torrents at once" finds the
reasoning — including the part about why cross-torrent chunk splicing does not work — already
written down.
