# Feria browser: shared behavior and app-specific spaces

Updated: 2026-10-07. Status: current reference for donor audits and branch prototypes, not an adopted production implementation. Preparation does not mean dispatch.

This revision supersedes the earlier taskbar session groups, session chooser, session terminology and visible Pop out/Return menu in this file. It also supersedes those presentation assumptions in Dots' report at `01cc5a48dfd9b69176380d8749d576ab1d35a661`. Preserve that report as source evidence; revise its proposed FRONTEND-INTERFACE against this contract.

## Purpose and ownership

Hemanth wants a complete functional browser inside Feria's site-as-app experience. Dots compares donor implementations and produces working backend prototypes on a Colosseum task branch. Codex reshapes production QML presentation and independently reviews returned commits and evidence. Nonvisual QML belongs to the behavior lane when needed; this is not a C++-only versus QML-only division.

The main goal is a strong browser foundation with Chrome-like correctness and reliability over a narrower Feria scope. QtWebEngine/Chromium remains the engine basis under evaluation; the task strengthens its application integration, not merely the visual shell. Supported browsing needs dependable navigation, independent pages, request ownership, privacy, lifecycle and recovery. A mock or source-copy count is not completion evidence. These prototypes should become reviewable functional candidates toward that foundation.

Standardize observable behavior, actions and lifetime ownership, not a generic Browser Tools drawer. A simple reference frontend proves the shared interface; app skins can change afterward. See the [current assignment](2026-10-07-dots-feria-inspection-brief.md) for baseline and batch scope. Keep category numbers from the [donor comparison](2026-10-07-feria-browser-donor-quality-comparison.md); old winners are candidates, not mandatory selections.

## Locked

- Each provider/site is its own Feria app. The Colosseum taskbar represents apps only, with no individual spaces or grouped session chooser/previews. Selecting an app restores its selected space; selecting its already foregrounded icon minimizes it and reveals the world underneath. Feria home launches/restores the app.
- Tabs live inside each app's retractable sidebar. Presentation categories are **watchspaces** for video, **readspaces** for reading and **soundspaces** for audio. Add actions are `watchspaces+`, `readspaces+` and `soundspaces+`, as appropriate. Shared tab machinery must not be duplicated into three implementations. No top tab bar.
- Rows show app/site icon and page title, with at most a slight active shade/glass response. No chunky capsules, obvious borders or heavy tab backgrounds. Exact layout, icons and skin belong to the app.
- Preserve the header: return-to-Feria, app identity, Back/Forward/Reload, URL button, Min/Max/X. Remove settings/sliders from that layer. Settings, extensions and spaces belong in the sidebar.
- The URL button unfolds an address row vertically below the header, integrated into the chrome rather than a detached floating panel. It must not cover the website.
- The docked sidebar may resize the website viewport when expanding/retracting. The normal outer embedded browser frame stays stable. No sidebar, settings or Colosseum-managed request UI overlays the website. Content authored by the remote website is separate from Colosseum chrome.
- Max presents the current live space full-size; Restore returns it to the prior embedded position and dimensions, retaining identity/live page state. Do not implement Max by changing the browser's normal Feria layout size. This presentation mode is distinct from site-requested media fullscreen.
- History and Downloads are full app pages with thumbnails and app-like presentation. They replace the content surface while selected; returning restores the selected live website space. Their layout does not authorize a preview-cache backend in batch 1-5.
- YouTube, Netflix, Manga Plus and other apps have their own sidebar skin, icons and navigation vocabulary over shared capabilities. Useful provider destinations can appear there instead of duplicating native site navigation. Provider adaptation is separate from the generic browser controller.
- Explicit new spaces open the provider start page. Site-requested destinations retain original semantics/context, including opener/form state. App identity is separate from current page origin, including during login redirects.
- Pause on app minimize/world switch; resume only media Colosseum automatically paused. Manually paused media stays paused. Vinyl remains the future deliberate play-across-world feature.
- Startup is fresh. Continue Watching/Reading/Listening remain the main return paths; browser restoration is explicit. Private spaces never enter normal history, closed-space records or persisted restore data.
- Browser default wallpaper sits behind the glass chrome rather than the Universes carousel. Controls should visually merge with Colosseum's skin.

## Constraints

- Inspect existing owners of canonical SessionStore identity, account/profile isolation, history, progress, bookmarks, downloads and preferences. Extend or replace them with migration/compatibility evidence; create no competing simultaneous registry/database just to match new vocabulary.
- Existing backend receives no sunk-effort preference. Choose stronger usable Qt/Chromium/donor machinery when evidence supports it. Distinguish direct source port, supported API wiring and behavioral adaptation.
- Compare relevant code in all five supplied donors; record absent/default/unsuitable cases honestly. Inspect wider donors when concretely relevant, without claiming comprehensive Chromium/Brave/CEF/Firefox inspection from searches. Vulkan is a graphics API, not a browser donor.
- Retain original request objects within supported adoption/deferral lifetime. Every asynchronous result is fenced to stable originating space/account/page generation, not whichever row is now selected.
- Per-space navigation, close, errors and recovery are observable. App selection, sidebar layout and focus cannot become engine ownership. Preserve original-page request identity through host/mode changes.
- Remove visible Pop out/Return and Open in separate window actions from this reference UI. That does not settle legitimate site-requested windows/dialogs/authentication hosting or disable the engine capability; report that seam.
- Do not hide a site's own navigation until equivalent destinations and fallback are established. Do not rebuild provider feeds/subscriptions/login/account truth in a parallel system.
- Record exact source revisions/paths, copied versus adapted code, retained notices, dependencies and file-specific licensing implications. No blanket donor license clearance is implied.
- Linux qualification does not prove Windows correctness. Codec-enabled Qt availability, DRM/protected providers, hardware authentication and audible output require their own actual evidence.

## Deferred

- General browser entry and extension implementation/management. Sidebar placement is a future seam; a mock affordance is not an extension runtime.
- Cached taskbar thumbnails, live hover peek, preview playback controls and polish. Old V2 session-preview ideas do not restore a grouped taskbar.
- Generic Tankoban/Biblio/Theatre desktop pop-outs, extra tab pinning/reorder/multi-select and unselected systems such as reader extraction, adblock or a new password vault.
- Final custom skins/provider DOM adapters for every site. The contract must permit them without requiring every adaptation before backend work.

## Open

These do not block the authorized audit and bounded prototype. Do not lock them through fixture behavior.

- Header X: close the whole app/all spaces or close the selected space? Row close targets only its named space. Expose distinct app-close and space-close semantics; app-wide close eventually honors each originating page's close handshake.
- Playback on switching spaces: whether departure pauses and return auto-resumes. This differs from locked app minimize/world-switch behavior. Keep suspension reasons/provenance separable; do not change background-space playback in navigation prototypes.
- Whether incoming requests automatically expand a retracted sidebar. Requests need origin identity, cancellation and accessible presentation outside the website regardless. Mock auto-reveal is illustrative.
- Site-requested window/dialog hosting after removal of visible Pop out controls; required platform-owned picker/auth UI versus the no-overlay rule. Audit interception/lifetime and report actual platform/API restrictions rather than assuming default Qt dialogs satisfy the presentation contract.

## Shared behavior interface

Names below describe semantics, not mandatory new classes/method spelling. Map them to live owners and return concrete prototype signatures.

| Surface | Observable state | Actions / ownership |
|---|---|---|
| App | Stable app/provider ID, display name, category, capabilities, selected space ID, visible/minimized state | Launch/restore/minimize; select space; app-scoped taskbar projection |
| Spaces | Stable space ID mapped to SessionStore identity, owning app/account/profile generation, privacy, title/icon/URL, selection | Create normal/private, select, request exact-space close, reopen normal, explicit restore |
| Navigation | Per-space native state, loading/progress, available Back/Forward, history entries, committed URL/page generation | Validated navigate, Back/Forward/Reload/Stop, history-offset for named space |
| Presentation host | Bound space ID, embedded/full-size mode, prior embedded geometry, host/transfer progress/capabilities | Full-size/restore without reload; viewport geometry changes without changing ownership |
| Requests | ID/type, originating space/page/account, origin, supported choices, pending/cancelled/resolved state | Resolve/cancel exact request within engine lifetime; summaries for sidebar presentation |
| Tools / app pages | Find/zoom/settings capabilities; canonical history/download models and record actions | Exact-space tools; history/download queries; app-page route separate from website navigation |
| Media / lifecycle | Observable playback/manual pause, suspension reasons, recovery/error state | Provenance-aware pause/resume; freeze/wake distinct from pause; bounded exact-view recovery/cancellation |
| Provider adaptation | Category/start page, destination IDs, capability exposure | Validated destination navigation, separately scoped site adaptation; no canonical account/history ownership |

Commands/callbacks use stable IDs. Reordering or reskinning cannot change their target. Row index, label, selected neighbor, sidebar width or panel location is never durable request identity. Relabeling the UI does not rename persisted IDs.

Per-page navigation history (category 3) is distinct from global account History. App presentation mode, native-window placement and website fullscreen are distinct. Batch 1-5 establishes necessary seams; detailed requests/downloads/recovery remain in their later category batches.

## Observable acceptance and reference limits

- Two spaces of one app preserve independent live fixture and native navigation state, without reload or duplicate canonical records; selection/actions target exact IDs.
- App-only taskbar projection and app-specific sidebar bind to shared state. Relabeling/reskinning needs no browser backend rewrite.
- Navigation/history-offset target the named space. Input follows Colosseum URL policy without unrestricted local-file acceptance or JavaScript history reconstruction.
- Foreground/background requests preserve original object/context; background creation does not steal selection. Window/dialog destinations and any hosting dependency are classified honestly.
- Managed controls remain outside the website viewport. Sidebar resizes viewport without changing outer frame; full-size/restore preserves fixture/geometry when that seam is prototyped.
- Account/private fences remain intact; no new persistence registry, automatic startup restore or extension runtime is introduced.

The [portable spaces reference](2026-10-07-feria-spaces-reference.html) is disposable simulated presentation, not production QML, engine verification or authority for Open choices. URLs, thumbnails, records and provider actions are fixtures. Header X and request auto-reveal illustrate proposals only. Its UI does not prove native close acceptance, media suspension or engine lifetime.

Reference verification: 18 focused headless Windows Edge checks passed, with no JavaScript errors. Checked category labels, add/select/close identity, mock navigation targeting, docked sidebar/address/settings/request geometry, full-size restoration of fixture state, app-page routes and 360px fit; desktop/narrow screenshots were inspected. This qualifies the simulated reference only. No Colosseum native build, QML runtime or WebEngine behavior was tested by these checks.
