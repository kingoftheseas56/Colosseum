# Anonymous collection viewer

The existing account-service serves this browser receiving surface at
`/collections/s/{share_id}`. `Handler()` embeds only `index.html`, `viewer.css`,
and `viewer.js`; tests and source are not addressable assets.

The viewer is signed-out: requests omit cookies, use `no-store`, never persist
content/passphrases in web storage, and never fetch external artwork. Text is
rendered as text nodes. Protected content is cleared on page exit; a restored
page fetches again. A saved edition file is an explicit copy that revocation
cannot recall. Saving a file is **not** native Keep Gift: the UI explains how to
import it through Collections → Open edition file, then Keep Gift.

The fixed schema includes only the allowlisted canonical/source identity,
text, ordering and sanitized PNG/JPEG cover fields accepted by the service.
Provider reopen payloads and extra response properties are not exported.

## Visual direction

The receiving surface adapts `qml/Theme.qml` (navy, restrained gold, muted
secondary text), using system UI type and an editorial system serif fallback.
The three presentation presets are `midnight`, `paper`, and `sea`. Gift opening
uses a small jacket/seal composition before free browsing, with no forced
one-title-at-a-time sequence. Mobile uses the same semantic order. Keyboard
focus, reduced motion and forced colors are supported. Scrollbars are hidden
per Colosseum's explicit product direction while document scrolling remains
native.

## Checks

From `server/account-service`:

    go test ./internal/httpserver/collectionviewer

From repository root (Node built-ins only):

    node --test server/account-service/internal/httpserver/collectionviewer/viewer.unit.test.cjs

Real Chromium fixture journeys (requires Playwright and an executable Chromium):

    node server/account-service/internal/httpserver/collectionviewer/viewer.test.cjs /tmp/collections-viewer-evidence

Set `CHROMIUM_PATH` when Chromium is not at `/usr/bin/chromium`.
The fixture test covers rendering, desktop/phone layout, keyboard focus,
passphrases, revocation, edition download, literal hostile text, oversized
responses, invalid images, network errors and no persistent browser storage.
It is a synthetic frontend contract test, **not** assembled service acceptance.
For manual fixture inspection, `VIEWER_FIXTURE_SERVER=1` starts only the
synthetic HTTP server and prints its local URL. Never deploy that fixture.

Final acceptance also requires a real account-service publication opened in
this browser, plus native file import/Keep Gift and offline restart. Record
those separately from unit/fixture results.
