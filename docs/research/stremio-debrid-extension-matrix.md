# Stremio Debrid Extension Matrix

Status: implementation baseline, 2026-08-16

This matrix records generic Stremio stream compatibility. A provider logo or an
extension name is not evidence of a provider integration. The add-on owns its
Real-Debrid, TorBox, AllDebrid, Premiumize, Debrid-Link, or other credentials;
Colosseum consumes the add-on manifest and stream response.

## Curated inventory

| Entry | Manifest URL | Catalog role | Stream role | Initial state |
|---|---|---|---|---|
| Torrentio | https://torrentio.strem.fun/manifest.json | featured source | enabled only after user consent | installed, removable |
| Comet | https://comet.elfhosted.com/manifest.json | stream rail | enabled only after user consent | catalog recommendation |
| MediaFusion | https://mediafusion.elfhosted.com/manifest.json | stream rail | enabled only after user consent | catalog recommendation |
| AIOStreams | https://aiostreams.elfhosted.com/stremio/manifest.json | stream rail | enabled only after user consent | catalog recommendation |
| Peerflix | https://peerflix.mov/manifest.json | stream rail | enabled only after user consent | catalog recommendation |
| NoTorrent | https://addon.notorrent2.workers.dev/manifest.json | stream rail and house well | direct stream source | installed, removable |
| WebStreamr | https://87d6a6ef6b58-webstreamrmbg.baby-beamup.club/manifest.json | stream rail | enabled only after user consent | catalog recommendation |
| Meteor | https://meteorfortheweebs.midnightignite.me/stremio/manifest.json | extras | included when its manifest advertises streams | catalog recommendation |
| Community manifest | user-installed manifest URL | arbitrary | same generic contract | user-controlled |

The manifest advertised resources, configuration flags, endpoint shape, and
returned row kinds must be filled from the live manifest during Task 6. The
matrix must distinguish Direct, Torrent, mixed, configuration-required,
unavailable, and non-stream outcomes.

## Current implementation baseline

- c752caf13cb45bfbb6b4f6b75046eb073db54e09 carries Direct/Torrent row
  normalization, nested request-header flattening, header cleanup, and primary
  player retry/reconnect routing.
- qml/AddonClient.js asks every enabled installed extension whose manifest
  matches the stream resource.
- native/engine/ExtensionsStore.cpp preserves the Stremio resource shape and
  behaviorHints.configurable/configurationRequired flags through manifest
  slimming.
- Removable stream wells are installed disabled on a fresh profile until the
  user enables them. Core catalogues and non-stream capabilities remain enabled.

## Fixture coverage

The deterministic fixture suite covers:

- root and configured-path manifest URLs;
- configured query parameters;
- Direct rows with and without request headers;
- Torrent rows with fileIdx;
- mixed Direct/Torrent/error/no-result responses;
- disabled and non-stream extensions;
- an arbitrary community manifest shape.

Fixture names are labels, not provider adapters. No credential-bearing URL or
real account data belongs in this repository.

## Live evidence

Live verification is intentionally separate from fixtures. It must use only
public manifests or user-provided configured manifest URLs, record a redacted
endpoint shape and dated result, and leave credentials and response tokens out
of repository files and logs.

## Live manifest evidence: 2026-08-16

The public manifests were reachable during implementation. This confirms
manifest discovery only; it does not claim that Colosseum authenticates with a
provider or that an unconfigured add-on can return a playable row.

| Entry | HTTP | Advertised resources | Configuration flags | Result |
|---|---:|---|---|---|
| Torrentio | 200 | stream | configurable | generic stream path eligible |
| Comet | 200 | stream | configurable | generic stream path eligible |
| MediaFusion | 200 | catalog, stream, meta | configurable | generic stream path eligible |
| AIOStreams | 200 | none in the public manifest | configurable, configurationRequired | final configured manifest required |
| Peerflix | 200 | stream | configurable | generic stream path eligible |
| NoTorrent | 200 | catalog, stream | none | generic stream path eligible |
| WebStreamr | 200 | stream | configurable | generic stream path eligible |
| Meteor | 200 | stream | configurable | generic stream path eligible |

No user-configured provider manifest URL was supplied for this run, so
debrid-authenticated playback smoke remains intentionally unclaimed. The
deterministic fixtures cover the Direct/Torrent and request-header paths that
the configured manifests feed.

A configuration-required base manifest with no advertised resources is kept
visible in the Theatre extension surface and opens the add-on's external
Configure endpoint. Colosseum does not install that unconfigured base URL as a
playback source; the final configured manifest URL must be pasted through the
existing install flow.
