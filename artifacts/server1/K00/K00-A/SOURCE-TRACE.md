# K00-A source trace

Oracle: `C:\b\Colosseum-Server-1.0-Planning-Pack\oracle\stremio-service-v4.21.1-server-bundle\server.js`

Whole-file SHA-256: `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`

The implementation is limited to the observed expressions below. The scenario probe under
`scenarios/oracle_k00_probe.mjs` replays those expressions in Node for raw differential output;
it is not a server implementation.

| Helper | Oracle callsite | Bounded behavior |
| --- | --- | --- |
| `Value::missing`, `Value::null`, `jsonStringify` | M106 12796-12809, 12813-12831 | Keep omitted distinct from null and preserve JSON omission/non-finite rules. |
| `jsNumber`, `jsTruthy` | M814 72445-72447; M172 18218-18224, 18234-18243 | Reproduce Number-like conversion and JavaScript falsiness used by option/default branches. |
| `jsParseInt` | M172 18266-18270; M176 18540-18545 | Parse the numeric prefix and preserve NaN-like malformed classification. |
| `finiteNumber`, `isPositiveInteger`, `isSafePositiveInteger` | M106 12815-12828; M400 34768-34774 | Keep numeric validation type-aware and reject non-finite/native-unsafe values. |
| `jsToInt32`, `jsToUint32` | M303 27755; M814 72489, 72547, 72685; M846 74736-74739 | Apply the relevant signed/unsigned 32-bit coercions only at compatibility boundaries. |
| `stringToBytes`, `bytesToString` | M303 27753-27779 | Preserve UTF-8 byte/string conversion used by torrent metadata paths. |
| `hasOwnProperty`, ordered `Value::Object`, `shallowExtend`, `Settings` | M106 12796-12804; M846 74740 | Preserve presence checks, insertion order, shallow replacement, and readonly serverVersion behavior. |
| `checkedSize` | M303 27761-27765, 27788-27793; M814 72468-72473 | Validate before constructing native sizes; never let malformed values reach an overflowing allocation. |

Assigned source ranges and hashes are recorded verbatim from the authenticated K00-A contract in
`PARALLEL-WORK-ITEMS.json`.
