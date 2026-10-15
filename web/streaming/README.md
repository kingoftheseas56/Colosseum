# Colosseum Streaming web surface

This is the durable web source for the Windows-first Streaming Home. `index.html` is the live discovery view; `preview.html` opens an illustrative design preview that needs no network or account. Both run from disk. The surface is designed for a Colosseum-owned WebView2 viewport, with provider playback taking the full viewport after handoff.

## Acceptance criteria for this source slice

Done when: the repository has a lasting, file-openable Streaming design source; the page uses Colosseum's visual tokens and works at desktop and narrow widths; Resume leads, with one provider's shelves, rich details, global search and Account Center available; live discovery comes from the supplied Stremio catalogue without claiming complete availability or playback; host data and actions have a documented WebView2 handoff; source syntax and the core preview, live, and host flows pass DOM checks. Native Qt hosting and provider playback are outside this HTML source slice.

The approved product contract is `Preflight-Architect/arcs/13-streaming-local-agents/specifications/PRODUCT-CONTRACT.md` in the adjacent planning workspace. The visual tokens mirror `qml/Theme.qml`: Fraunces, Segoe UI, dark glass, and sparing gold.

## Current boundary

- Resume comes only from Colosseum's progress data. The live standalone page shows an honest empty state until a host supplies progress. The preview contains labelled sample progress.
- The live page reads the public [Streaming Catalogs addon](https://stremio-addons.net/addons/streaming-catalogs) through its Stremio manifest and `catalog` resources. It loads Netflix first, then other selected services on demand or during global search. Its lists are discovery data, not a complete provider library or proof of entitlement.
- The addon manifest exposes `catalog`, not `stream` or `meta`. Provider login, entitlement checks, title navigation and DRM playback remain on the provider website inside WebView2.
- Search groups loaded titles by Unicode-normalized title and media type, with a release-year guard for remakes. A host may supply `canonicalId` to join alternate titles. Active Resume decides the primary provider when available; otherwise the user provider order wins. Opening Search loads all supported service catalogs.
- Account Center is a master-detail UI receiving connection/profile/sync state from Colosseum. When supplied, it shows provider profile choices and progress conflicts for review. The standalone view claims no account is connected. Login always opens the provider's own UI.

## Host integration contract

After navigation completes, inject data with `window.ColosseumStreaming.mount(data)`. Example shape:

```js
window.ColosseumStreaming.mount({
  accountInitial: 'H',
  providers: [
    { id: 'nfx', name: 'Netflix', mark: 'N' },
    { id: 'amp', name: 'Prime Video', mark: 'P' }
  ],
  connections: {
    nfx: { status: 'connected', profile: 'Hemanth', sync: 'Up to date' },
    amp: { status: 'attention', profile: 'Main', profiles: ['Main', 'Kids'], sync: 'Review needed', conflicts: [{ id: 'c1', label: 'Review episode progress' }] }
  },
  resumes: [
    { id: 'tt1234567', providerId: 'nfx', type: 'series', title: 'Example title', episode: 'S1 E3', progress: 0.42, remaining: '28 min left' }
  ],
  catalogs: {
    nfx: [{ id: 'series', name: 'Series to discover', titles: [
      { id: 'tt1234567', canonicalId: 'example-title', providerId: 'nfx', type: 'series', title: 'Example title', description: '', image: 'https://example.com/image.jpg' }
    ] }]
  }
});
```

`mount` replaces standalone data; it does not connect accounts or navigate providers. No credentials, cookies or DRM material are passed to this surface. Provider order in `providers` drives the icon rail and search priority. The host may call `mount` again after progress or connection changes.

User actions emit `window` event `colosseum:streaming-action`, and, inside WebView2, post the same object through `window.chrome.webview.postMessage`. For a host choosing a direct JS callback, call `window.ColosseumStreaming.onAction(handler)`; this takes priority over WebView2 messaging. Actions are `resume`, `open-title`, `episode`, `open-provider`, `connect-service`, `manage-service`, `reorder-services`, `select-profile`, `review-conflict`, and `watchlist` when enabled by host data. Each has `providerId`, `titleId`, `episodeId`, `title`, and `mediaType` when applicable. Reordering includes `providerOrder`; profile and conflict actions include `profileId` and `conflictId`. The host owns entitlement verification, canonical title-to-provider URL resolution, provider navigation, progress sync, and error handling.

The live standalone source currently uses `https://catalog.ers.pw/manifest.json`, one public instance linked from the community listing. The host should choose and validate its configured instance and country settings, then pass normalized catalog data through `mount`. Public addon availability and metadata can change independently of Colosseum.

## Verification scope

Run `npm ci` then `npm run check` in this folder to validate syntax and the preview, host and live DOM flows. The tests use a local DOM emulator; they do not claim pixel-perfect rendering, WebView2 integration, or provider playback.
