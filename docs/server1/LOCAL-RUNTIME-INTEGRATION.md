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

Catalog advertisement snapshots the saved `localAddonEnabled` setting at startup,
using JavaScript truthiness as in M564. A settings POST persists the choice, but
the manifest changes only after restart. This setting alone never enables roots,
automatic scanning or online providers. The production-host regression checks
20 initial/restart cases against the pinned manifest fixtures, including absent,
null, boolean, numeric, string, array and object values.

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

The default composition embeds three independently generated black/silent samples
at the original route names. No upstream sample payload is bundled. Their
codec/container/channel assumptions match the profiling inputs, but their bytes,
hashes and audio durations deliberately differ. See
[generated sample provenance and qualification](GENERATED-PROFILER-SAMPLES.md).

`sampleDirectory` or the host's `--sample-dir` overrides that default with an
explicit directory of compatible pinned M934 files. Only three known filenames
are eligible; type, size and SHA256 metadata are checked before serving, and
symlink files are rejected. Missing or incorrect files in an explicit directory
return 404, without silently falling back to generated media. No directory
listing or arbitrary file serving is added. The low-level route API also offers
`SampleMode::ExternalPinnedOnly` with an empty directory for no-assets tests.

`/device-info` reports only verified acceleration, rather than guessing from
installed encoders. `/hwaccel-profiler` runs real conversions using isolated HLS
services and the loopback sample routes; it does not compete with playback's
converter registry. No viable profile produces the source-compatible error
response. Each attempt has a bounded deadline, and a timeout never counts as
hardware success.

The exact-oracle tests materialize upstream sample bytes only in disposable test
folders from an explicitly provided verified oracle. Production builds contain
the generated set, not those upstream bytes. Real GPU success, Windows and macOS
devices remain separate qualification tasks.

## Runtime compatibility

Oracle tests use the pinned portable Node22.16.0 profile described in
[PINNED-NODE22-RUNTIME.md](PINNED-NODE22-RUNTIME.md). Production code does not spawn
Node or interpret the original server JavaScript. Earlier Node24 aggregate results
remain historical evidence, not proof of interchangeability between runtimes.
