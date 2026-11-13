# Native local runtime and profiling integration

The production composition owns `LocalAddonRuntime` and `HardwareProfiler` on
its Qt application lane. The local addon is mounted before the torrent catch-all.
The profiler starts only after a successful listener bind and follows source
startup detection/skip rules. Both owners are destroyed before their engine or
media dependencies.

## Explicit local indexing

`ServerRuntimeConfig` accepts `localRoots`, `localDatabasePath` and
`localMetadataEnabled`. Defaults are no roots, no database and no online
recognition. The application does not automatically scan HOME or follow symlinks.
A nonempty root list requires an explicit database path. Supplying only a database
reloads existing entries and metadata without scanning additional roots.

The standalone test host exposes equivalent options:

```
server1_host --serve --port 0 --cache /path/to/cache \
  --local-root /path/to/authorized/media --local-db /path/to/local-addon.db
```

`--local-root` is repeatable. Files are indexed through native filename parsing,
torrent metadata decoding, recognition and append-log persistence. Without online
recognition, synthetic or already known torrent entries remain usable; an unknown
plain media filename is not falsely assigned a movie identity.

`--local-metadata` explicitly enables the pinned source Cinemeta and IMDb
providers. They receive parsed titles/year/type or IMDb identifiers, never local
file paths. Leave the option off unless that transmission is intended. Tests use
synthetic names and loopback providers, not user media or public recognition.

Uncached `bt:` metadata requests use the same engine-registry creation/statistics
path as torrent routes. Cancelling one addon request does not remove a shared
engine. Addon callbacks are suppressed or completed safely when their owner ends.

## Sample media and hardware profiling

No source sample payload is bundled in this checkpoint. `sampleDirectory` or the
host's `--sample-dir` supplies an explicit directory of compatible pinned sample
files. Only three known filenames are eligible; type, size and SHA256 metadata
are checked before serving, and symlink files are rejected. No directory listing
or arbitrary file serving is added by these routes.

Missing, incorrect or absent samples return 404. `/device-info` still works and
reports no verified acceleration rather than guessing from installed encoders.
`/hwaccel-profiler` runs real conversion attempts through the loopback HLS routes;
no viable profile produces the source-compatible error response. Each request has
a bounded deadline, and a timeout never counts as hardware success.

The exact-oracle tests materialize sample bytes only in disposable test folders
from an explicitly provided verified oracle. Production builds contain integrity
metadata and native code, not those media bytes. Real GPU success, Windows and
macOS devices remain separate qualification tasks.

## Runtime compatibility

Oracle tests use the pinned portable Node22.16.0 profile described in
[PINNED-NODE22-RUNTIME.md](PINNED-NODE22-RUNTIME.md). Production code does not spawn
Node or interpret the original server JavaScript. Earlier Node24 aggregate results
remain historical evidence, not proof of interchangeability between runtimes.
