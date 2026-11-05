# Collections website staging

`stage_site.py` generates a new static-site artifact from a reviewed public-site
baseline and committed product source. It does not deploy, start a preview,
change Cloudflare configuration, run tests, or establish browser/header behavior.
The current website has no Git identity: its actual file hashes are recorded
separately from the product source commit. Keep machine paths, deployment receipts
and runtime evidence in the task's private record.

```text
python web/collections-sharing/stage_site.py --site-base <reviewed-site> --output <new-artifact>
```

The output must not exist and must be disjoint from both the site baseline and
product checkout after path resolution. Symlinks and Windows reparse points are
rejected, including existing path ancestors. A failure retains partial output
without a completion manifest; investigate and choose a different new output.
The tool never deletes or overwrites an old artifact as a retry shortcut. Use
the original reviewed baseline rather than feeding a generated artifact back in.
The product tree must be clean and committed; normal Git metadata reads identify
its source revision. No Git repository is created in the website baseline.

The stager excludes private `preview.html`/review-hub material, VCS, credentials,
local databases and build caches. It rejects review-hub markup in the homepage.
The legitimate referenced `current-ui-mock` public assets remain included.
Existing installer, release, demo and feed redirects, tribute cache rules and
unrelated files are retained. Existing custom-domain Workers, including private
preview guards, are outside this artifact and must remain unchanged.

## Integration contract

The canonical viewer is under
`server/account-service/internal/httpserver/collectionviewer/`. The stager copies
its JS/CSS byte-for-byte into `collections/assets/`, stages the HTML once at
`collections/viewer.html`, and requires one unique occurrence of each body marker:

```html
<body data-api-origin="" data-site-brand="">
```

Only those values are injected: the HTTPS API origin (defaults to
`https://api.getcolosseum.world`; override with `--api-origin` for an isolated
stage) and
`data-site-brand="getcolosseum"`. The first-party Cold Ripple, Fraunces and mark
files come from the reviewed baseline. Unmarked legacy service HTML continues to
use its self-contained fallback when edge enforcement is disabled. Public identity is always
`https://getcolosseum.world/c/<43-character-id>`; URL/query content never selects
the API origin.

The generated routes precede existing fallbacks:

```text
/downloads /index.html 200
/c/* /collections/viewer.html 200
```

The existing router initializes Downloads for the exact `/downloads` pathname;
other initial routes and subsequent hash navigation retain their existing owner.
An insertion inside the existing Downloads panel supplies the return/copy notice.
`downloads-return.js` accepts one strict 43-character `collection` parameter,
rejects duplicates/invalid identities and ignores all other values, including
redirect parameters. It constructs only a canonical collection URL, renders
literal text, and offers a manual copy fallback. It never writes cookies/storage,
launches the app or redirects automatically.

## Scoped privacy and Android gate

The preview CSP permits only same-origin scripts/styles/fonts/images, bounded
data images, and the exact configured API for connections. No external analytics
script or connection is allowed. The policy plus no-referrer, no-store,
nosniff and framing restrictions is authored for `/c/*`, `/collections/viewer.html`
and the extensionless viewer alias. Viewer assets receive scoped cache/referrer
and type protections.

Downloads receives its own policy at `/downloads`, allowing first-party scripts
and SHA-256 hashes of its two existing inline scripts after the router edit.
Hashes use HTML-normalized line endings. External Insights scripts/connections
remain excluded so injected analytics cannot read a collection query. Existing
inline styles, first-party frames/demos and resources remain permitted; unrelated
site pages receive no new policy. There is no broadly scoped policy on
`/index.html` that would silently change every homepage visit.

These are authored rules, not proof of response headers after a Pages rewrite or
an existing Worker. [Pages headers](https://developers.cloudflare.com/pages/configuration/headers/)
can combine matching rules, and
[Pages redirects](https://developers.cloudflare.com/pages/configuration/redirects/)
run before headers. Actual request/rewrite-target behavior, custom-domain
analytics blocking, browser rendering, navigation and old routes require manual
qualification before any separately authorized promotion. Do not broaden the
privacy policy to accommodate analytics.

`--assetlinks <qualified-public-json>` is optional for a desktop/local artifact.
Without it, no association file or placeholder/debug identity is emitted, and any
baseline association file is excluded pending qualification. Android verified
HTTPS association remains blocked. With the option, the caller must supply
independently qualified public metadata from the actual approved release package
and app-signing certificate, never keys or an upload/debug certificate. The tool
validates bounded Android association JSON and copies its bytes exactly; syntax
does not prove release identity. No package/certificate is inferred from Qt's
manifest template. Before Android completion, valid JSON must be served at the
exact well-known URL on apex and www without redirect/login and qualified with
the approved signed app.

## Artifact record

`collections-stage-manifest.json` is written last. It records the clean product
revision, relevant product source hashes, included baseline file hashes,
exclusions, output file hashes, changed paths, marker/origin contract and explicit
Android/runtime/deployment gates. Its bytes are deterministic for identical
inputs and source. It contains no invented website commit, absolute machine
paths, credentials or timestamp. The manifest's own hash is intentionally not
self-referential. Its presence means generation completed, not release approval.
`collections-stage.diff` records the scoped textual integration changes with
relative paths and is hashed by the manifest.

No new test suite accompanies this task: testing, browser/Pages/header checks and
installed activation remain with Hemanth under the current task instruction.
