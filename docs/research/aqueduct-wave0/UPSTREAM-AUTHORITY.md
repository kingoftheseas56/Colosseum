# W00 — Upstream authority

## Specimen identity

Aqueduct's behavioral oracle is the exact Stremio payload Colosseum currently packages, not Stremio `master` and not the Rust streaming-server rewrite.

| Artifact | Authority |
|---|---|
| Server | Stremio Server `4.20.17` desktop bundle |
| `server.js` size | `6,631,104` bytes |
| `server.js` SHA-256 | `567A397BB11B788571BF1750FD05DD78927F97BEC0C9DDEAA6D9CC1ECCEE3922` |
| `stremio-runtime.exe` SHA-256 | `8AD810919DF76741A153DBF28180A84F7AB395EA3DA2534374A10F0E6DCA7E3B` |
| Webpack modules | `1,310`, positional IDs `0..1309` |
| Split verification | byte-exact round trip |

The same two hashes were recorded by `docs/research/tankorent2-phase0/00-specimen.md` on 2026-08-07 and were re-verified against the installed Colosseum payload on 2026-09-02.

## Authority order

1. Exact `4.20.17` bundled bytes and runtime behavior.
2. Exact dependency revisions encoded in that bundle/package manifest.
3. Upstream source at those revisions where publicly recoverable.
4. Existing 2026-08-07 runtime-validated forensic research.
5. Current upstream projects only as explanatory material, never as a silent semantic replacement.

## High-value module map

| Module | Role | Aqueduct packets |
|---:|---|---|
| 105 | settings/defaults/persistence | W05 |
| 172 | EngineFS registry, routes, stats, stream lifecycle | W07-W09, W21-W22 |
| 414 | cache/disk cleanup policy | W06 |
| 564 | process/server entry wiring and top-level mounts | W03-W05, W23-W31, W33-W36 |
| 613/614/625 | PeerSearch, DHT and tracker discovery | W14 |
| 667+ | legacy HLS middleware | W23-W24 |
| 805 | generic proxy | W27 |
| 816 | `torrent-stream` engine and scheduler | W10-W19 |
| 846/847 | persistent and circular piece stores | W12-W13 |
| 848 | FileStream moving-window reader | W20 |
| 853 | 16 KiB piece block reservation buffer | W17 |
| 855 | HLS v2 HTTP router | W25 |
| 944 | casting router and transcode bridge | W29 |
| 1024/1025 | local addon plus addon HTTP contract | W30 |
| 1088 | NZB router/core | W36 |
| 1121 | RAR router | W33 |
| 1234 | ZIP router | W34 |
| 1285 | 7z router | W34 |
| 1293 | TAR router | W35 |
| 1300 | TGZ router | W35 |
| 1306 | FTP router | W36 |

## Encoded custom dependency pins

The bundle's package manifest pins the following Stremio-specific components. These exact revisions are the semantic references for their packet; a newer branch must not be substituted without an explicit parity decision.

| Component | Encoded revision |
|---|---|
| `enginefs` | `Stremio/enginefs@3a70b36f873307cd83fb3178bb891f73cf73aa87` |
| `torrent-stream` | `4d9eaff84e3b7a1008314daa007d5feb6fa26388` |
| `stremio-hls-middleware` | `c84254f4e19e7a35017132385702e81b5e6fc674` |
| `stremio-local-addon` | `be2e9c25f4d4bda4e76cdeccc523ee374e1f4b47` |
| `rar-http` | `7e661b7a001ba45fd6cf5edfe688f7abd1716a9c` |
| `zip-http` | `897a59fabae5809f72a0426262eda6225aa971bd` |
| `nzb-http` | `b3c03a3bb8a8b03e9689cc85e83629fc30aa97fb` |
| `7z-http` | `f24d29035ddee487c280e4fe4609d21af7920522` |
| `tar-http` | `502c3dafa2d50fe9a6b6e54e798b13305f4665d8` |
| `gzip-http` | `876b9cea2ad83923513d9c4d85bb7d3f794590dc` |
| `ftp-http` | `8a9ae06317401452aeccdb5e795c8985354c19d9` |

`enginefs` at its pinned revision identifies itself as version `4.15.1`. Public GitHub access to the encoded `torrent-stream` revision was not resolvable during W00, so its bundled module bytes remain the authority rather than a guessed repository state.

## License and provenance rule

The `stremio-server` package manifest inside 4.20.17 declares MIT. That does not automatically prove the license of every separately sourced dependency. Wave 0 therefore records provenance separately from licensing:

- preserve Stremio and dependency copyright/license notices for any translated source;
- do not infer a dependency license from the parent bundle;
- `enginefs`'s pinned public tree has no root `LICENSE` file and its package manifest does not declare a license, so its attribution/license status is **unresolved pending explicit review**;
- the inaccessible public `torrent-stream` revision is likewise **unresolved pending explicit review**;
- W41 may remove the foreign runtime only after the final translated-source attribution inventory is complete.

This is a packaging/legal gate, not permission to omit behavior from the port.

## Behavioral invariants already established

The August forensic work proved that the EngineFS listener starts at 11470 and retries through 11474, while six literal `11470` sites include internal self-references. The lab therefore changes all six literals to 11480 in a copy and leaves 11474 unchanged.

The exact 4.20.17 settings defaults relevant to BitTorrent are 55 max connections, 20 s handshake timeout, 4 s request timeout, 2.5 MiB/s soft limit, 3.5 MiB/s hard limit, and 5 minimum stable peers. Colosseum may override values at runtime, but oracle comparisons distinguish defaults from overrides.

The source-port matrix is exhaustive over the 90 mechanically extracted HTTP/router registrations plus the internal behavioral modules that materially implement those routes.
