# K13-A source trace

Worker: `K13-A`
Packet: `K13`
Oracle: `C:\b\Colosseum-Server-1.0-Planning-Pack\oracle\stremio-service-v4.21.1-server-bundle\server.js`
Oracle SHA-256: `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`

The current base already contains the accepted K00 compatibility value contract. K13-A consumes
`server1::policy::Value`, `server1::policy::Settings`, `hasOwnProperty`, `shallowExtend`,
`jsTruthy`, and `isPositiveInteger`; it owns only the settings-store implementation. K13-B owns
cache policy, disk-space ports, tests, and packet manifests.

## Authenticated source ranges

| Module | Oracle lines | SHA-256 | K13-A use |
| --- | ---: | --- | --- |
| M106 | 12792-12833 | `12b723abb6dd833967bb40ad214679a4584bbe3c8b6d2b97cafbc19e3fc52179` | Settings object, defaults, load/merge, readonly version, async persistence. |
| M194 | 19675-19682 | `b55829ef3824da2c446073bfd42c764b6871effd46136f64fd67d2a6c1c69843` | App-path selection is an upstream caller responsibility; the store consumes the resolved app path. |
| M400 | 34767-34775 | `868936ff7407bd2a05aecd8205ae465b097b09656c09b3c3265b8188949c5fde` | Positive-integer validation for numeric defaults. |
| M414 | 35959-36170 | `16b1c32e135ce079b4d718a627c0a6405f34bb0878a6804f1dbecd51a5b5770d` | Cache option values and delayed cleaning are K13-B; K13-A preserves the settings values passed to that seam. |
| M564 | 46578-46979 | `d02004f3597ab40da72799e55f852428db0946210ee93dba9533800d1b0b0569` | Settings construction after app-path setup; GET/POST consumers call extend, cache option update, and save. |

## Behavior translated

- Start with a `serverVersion` accessor that ignores writes. Set `appPath`, then apply presence-
  based defaults in M106 order.
- `cacheSize` defaults to 0 when caching is disabled, 0 on Android, otherwise 2 GiB; a persisted
  or explicit property wins after default construction.
- Positive-integer settings use the M400 predicate. String, zero, fractional, negative, and
  non-finite values fall back to the exact M106 defaults.
- `cacheRoot`, `transcodeProfile`, and `transcodeMaxWidth` use property-presence checks; the
  profile default is JSON null. `allTranscodeProfiles` is reset to an empty array.
- `remoteHttps` requires a string. `localAddonEnabled` and `proxyStreamsEnabled` preserve the
  JS truthy-and-value shape after presence is established. `transcodeHorsepower` uses JS truthiness.
- The settings file is `(SETTINGS_PATH || appPath)/server-settings.json`. A valid object is merged
  shallowly after defaults, unknown fields are retained, and the loaded object is saved back.
  Missing or corrupt input logs an error and leaves defaults intact without overwriting the file.
- Save serialization follows JSON.stringify's observable rules for this surface: four-space
  indentation, omitted missing object members, array missing values as null, and non-finite
  numbers as JSON null. `serverVersion` remains the package version after load or override.

## Interface assumption and Sol correction

The original worker assumption was cpp-only ownership: the source included
`server1/settings/SettingsStore.h`, while K13-A owned only
`native/colosseum_server_v1/src/settings/SettingsStore.cpp`. Live checkout evidence showed that
the required production header did not exist and no other worker owned it, so the source could not
be compiled or consumed by `EffectiveSettingsContract`.

Under the explicit Sol correction, K13-A now owns the smallest required declaration surface at
`native/colosseum_server_v1/include/server1/settings/SettingsStore.h`. It is the exact declaration
that was first staged under the packet-local probe, with no API expansion. The probe now resolves
the production header; the old duplicate temporary header was removed.

Affected workers: `K13-A` produces the declaration and implementation; `K13-B` consumes the
`EffectiveSettingsContract` and converges cache policy/tests/manifests. Affected barrier:
`B-W2B`. Integration topology is unchanged: K13-A remains a producer, K13-B remains downstream,
and INT-W2 remains the sole integration owner.

The settings store is constructed with a resolved app path and package version. The caller maps
process/platform state to `android` and `disableCaching`; the store does not invent an app path,
cache enumeration, disk-space probe, or cache cleanup policy. K13-B consumes `value()`/`find()` and
the path/value contract, then owns the `CachePolicy` and `DiskSpace` seam.

## Assigned evidence boundary

K13-01 is executable in this worker. K13-03's cache-directory enumeration, equal-atime eviction,
disk-space adjustment, delayed cleaning, and fallback cache-directory decisions belong to K13-B's
cache policy and disk-space files; this worker can only prove settings load/merge/persistence inputs
to that seam. No runtime or full-server claim is made here.
