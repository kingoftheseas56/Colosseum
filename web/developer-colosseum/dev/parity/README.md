# Arc 54 parity rig

Build `colosseum` and `lanista` from this checkout first. The rig launches two
tagged copies of that one binary, copies one frozen snapshot of the current
real-library metadata into each disposable AppData root, and verifies their
PIDs and resolved storage roots through Lanista before capturing anything.
It uses the Harness session runner's tagged AppData seed placement, then keeps
each attached process alive long enough to capture both viewport sizes.

From the repository root:

```powershell
node web/developer-colosseum/dev/parity/parity.mjs '{"name":"world","world":"Theatre","tab":"discover"}' theatre-discover
node web/developer-colosseum/dev/parity/parity.mjs '{"name":"world","world":"Theatre","tab":"library"}' theatre-library
node web/developer-colosseum/dev/parity/parity.mjs '{"name":"world","world":"Theatre","tab":"movies"}' theatre-movies
node web/developer-colosseum/dev/parity/parity.mjs '{"name":"home"}' home
node web/developer-colosseum/dev/parity/parity.mjs '{"name":"search","scope":"Theatre"}' theatre-search
node web/developer-colosseum/dev/parity/parity.mjs '{"name":"seeAll","world":"Theatre","tab":"movies","title":"Top 10","route":{"v":1,"world":"Theatre","source":"catalogue","facet":{"tab":"movies","rowKey":"top10","medium":"movie"},"explicit":false,"pageSize":24}}' theatre-seeall
```

The QML side executes a registered Colosseum Harness journey against its tagged
Lanista pipe; `qml-journey.json` records the result. Home and Theatre Discover
have included journeys. The rig then drives Theatre's
Library, Movies, Shows, or Anime tab with Lanista and confirms the selected
tab. For another eligible page, register a Lanista scenario and select it with
`--journey <name>`. Player, reader, and
Vault pages are refused because their QML remains native owned.
Connections and Update are refused because this build no longer has their
reference QML.

`--seed <dir>` uses a different real-library AppData copy. The default is
`%APPDATA%\Brotherhood\Colosseum`. The snapshot includes profile, session,
catalogue, vault and video index data; it excludes downloaded media, images,
logs and caches. The manifest contains only the file count, byte count and
SHA-256 of the frozen copy. Python with Pillow is required to normalize device-pixel
screenshots and generate the heatmap. The private captures, logs, seed and reports live in
`dev/parity/out/<label>/`, which is Git ignored. Both sizes have self-contained
HTML reports. The heatmap and pixel score are visual aids only.

For other pages, create `pages/<label>/allow.json`. Every allowance must name
the exact text/control and side, a count, a one-line reason, and the contract
section that permits it. Layout allowances additionally need `kind`, `text`
and `maxDelta`. Empty arrays mean no differences are allowed. Examples:

```json
{
  "words": [{ "side": "qml", "text": "Example", "count": 1,
              "reason": "Native-only status label", "contract": "§4.2" }],
  "controls": [],
  "layout": [{ "kind": "words", "text": "Example", "maxDelta": 12,
               "reason": "Native title inset", "contract": "§4.2" }]
}
```

For a native route beyond the initial world/tab, pass each named Lanista click
in navigation order with `--qml-click <objectName>`, or click an exactly matched
visible focus target with `--qml-label <accessibleName>`. `--qml-find-label`
scrolls the world's board in bounded steps to find a deeper target such as a
See All shelf. `--qml-scroll <objectName>:<wheelDelta>` sets an explicit scroll
position. The web route is always
driven through the same CDP transport as `dev/e2e/attach.mjs`. Unsupported or
unresolved QML navigation fails as infrastructure rather than producing a
misleading parity result.

To demonstrate sensitivity without altering tracked web source, inject a wrong
word and hide a real focus target in the web process's DOM copy:

```powershell
node web/developer-colosseum/dev/parity/parity.mjs '{"name":"world","world":"Theatre","tab":"discover"}' theatre-discover-defect --allow web/developer-colosseum/dev/parity/pages/theatre-discover/allow.json --inject-word Home --hide-control Theatre
```

The exit codes are 0 for parity PASS, 1 for parity FAIL, and 2 for capture or
navigation infrastructure failure. `node web/developer-colosseum/dev/parity/selftest.mjs`
checks the comparator's known-match and two planted-defect behavior without
launching the app. That selftest is a mechanical control, not a canonical-page
PASS. Keep the QML page until its live report passes or every failure is backed
by a contract-cited allowance.
