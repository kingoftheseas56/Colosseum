# K01-A source trace

Oracle: `C:\b\Colosseum-Server-1.0-Planning-Pack\oracle\stremio-service-v4.21.1-server-bundle\server.js`

Whole-file SHA-256: `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`

The worker translates only the following authenticated behaviors. The K01 test source is the
executable contract for metadata fields, parser rejection boundaries, real/virtual geometry, and
the separate wire-block coordinate domain.

| Case | Oracle authority | Required behavior |
| --- | --- | --- |
| K01-01 | M303 `27748-27805`; M814 `72439-72764` | Preserve UTF-8 file/name preference, file order, zero-length offsets, cumulative lengths, real verification pieces, virtual-piece boundary mapping, and final short piece/block lengths. |
| K01-02 | M814 `72439-72764` | Use a 512 KiB virtual piece only when the real piece length is greater than 524288 and divisible by 524288. Keep verification, virtual, and wire-block coordinate types distinct; the test also checks round-trip and bounds properties over 119 deterministic total/real-length vectors. |
| K01-03 | M303 `27748-27805` (including `27769`); M815 `72764-72784`; M842 `74455-74473` | Flatten announce-list order without inventing deduplication, prefer `.utf-8` fields, preserve percent-encoded names, preserve absent versus present-false/true `info.private` values before boolean coercion, reject invalid metainfo/numeric lengths, and hash the canonical bdecode/re-encoded `info` value. |

Authenticated source range hashes:

- M303 `27748-27805`: `eb8b00c36b67354e28185cc83098831121a3510c31f9d01700d2991f353d6026`
- M814 `72439-72764`: `05eba72a8229b9705b0e657af5a4223fb88809f1b90325614cb33a5ecefb005e`
- M815 `72764-72784`: `1712371c69dd5878a58aa46eb7d8f712ad3803f4cc365039350ec074d0ff8583`
- M842 `74455-74473`: `5c8e4cf281005ff8de0dc50d8c76680e46b4053956ff6426d72f96543ffb62b5`

Dependencies accepted before this worker: B-W2A (`0519371ad02e20982b8d054c64008d4ae29b3926`,
K00 compatibility values) and B-W1C (`730ec57335c14929b6c937e7a0d53546473231ca`, trace comparator).

The differential probes will be packet-local under `artifacts/server1/K01/K01-A/`; no shared
manifest, aggregate CMake file, or runtime surface is changed by K01-A.
