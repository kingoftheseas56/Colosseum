# Concrete frontend interface, categories 1–5

Supersedes the proposed presentation interface in report 01cc5a48. The written contract at baseline2916fe85 governs; the HTML remains illustrative. This interface accompanies real native/QML components, not mock browser state.

## Canonical app and space projection

Existing native SessionStore (`Sessions`) owns all IDs and records. Added Q_INVOKABLE signatures:

```cpp
QVariantList browserSpaces(const QString &provider) const;
QString selectedBrowser(const QString &provider) const;
QVariantList browserApps() const;
bool activateBrowserApp(const QString &provider);
```

- browserSpaces returns canonical browser records `{id,target,title,...}` filtered by stable provider ID
- target retains providerId, mode, privateBrowsing and current URL; ID never derives from URL/title/index
- browserApps returns one item per provider: `{providerId,title,icon,mode,selectedSpaceId,foreground}`. It excludes individual-space lists from taskbar projection
- selectedBrowser returns the active space for that provider or its existing MRU, without a second selection database
- activateBrowserApp toggles the foreground app to the underlying world or restores its selected space; it returns false for an app with no open spaces. Launching a new app remains the existing provider launcher
- observe Sessions.revision and activeId in QML bindings. Read-only projection methods alone do not create a QML dependency; setActive updates MRU before emitting activeChangedProp, so both signals matter for the final selected/foreground projection
- Sessions.switchTo(id) selects a specific sidebar row. Account change invalidates browser records. Existing close semantics are retained; header-X meaning is not assigned here

Labels watchspaces/readspaces/soundspaces and + actions are presentation over the same provider ID and native records. An app icon identifies the provider even after an SSO redirect. Row labels/icons and future skin changes do not alter request targeting.

## Per-space navigation

Existing retained FeriaSessionDeck gains:

```qml
browserForSession(id)                         // live view or null
navigationForSession(id)                      // FeriaNavigation or null
back(id)                                     // bool: command accepted
forward(id)                                  // bool
reload(id)                                   // bool
stop(id)                                     // bool; false when not loading
historyOffset(id, integerOffset, revision)    // bool
navigateInput(id, text, searchEngine)         // bool
newSpace(sourceId, privateBrowsing, activate) // canonical new ID or ""
```

Boolean results mean synchronous policy/target/action validation passed, not a successfully loaded page. Observe native loading/result state for completion. No callback resolves against whichever space happens to be selected later.

Each live view exposes `navigation`:

```qml
usable: bool
canGoBack: bool
canGoForward: bool
loading: bool
progress: int
historyItems: QAbstractItemModel
backCount: int
forwardCount: int
historyRevision: int
```

Native history delegate roles are `title`, `url`, `icon`, `offset`. Bind a ListView/Instantiator directly; do not use array indexing/get/count on the model. A chooser captures origin space ID and revision. If it stays open across model reset/navigation, close or rebuild it and use a fresh revision. historyOffset rejects stale tokens. Toolbar Back is not equivalent to offset -1 under Chromium's skip-on-back policy.

The compatibility wrappers `pageBack()`, `pageForward()`, `navCanGoBack`, `navCanGoForward` remain available to existing product QML. Old JavaScript navigation bookkeeping is removed, not exposed as another source of truth.

## Typed input and explicit new spaces

FeriaBrowserPolicy keeps `QUrl navigationUrl(QString)` and `QUrl omniboxUrl(QString,QString searchEngine="duckduckgo")`. Input is remote-only, bounded, control-character/credential/local-path guarded. Search values are percent-encoded as a whole. This is typed navigation policy; engine-created about:blank/blob requests still use the existing separate allowlist.

newSpace uses the canonical provider start destination from existing PorticoData.P, or a validated descriptor startUrl supplied by the caller/fixture. It never treats the current article/video URL as the provider homepage. The legacy newTab facade remains for old callers; new UI should provide sourceId explicitly. Original site requests use openWindow(sourceId,request), not navigateInput/newSpace or a reconstructed URL.

## Original request lifetime

The request's actual `destination` distinguishes tab/background/window/dialog. It is consumed synchronously inside its engine handler, in a fresh correctly profiled view. The request must not be stored in a list, passed through a timer/Promise/asynchronous Loader, or recreated from requestedUrl. Background creation leaves current selection/focus unchanged. A failed factory/profile/ownership fence cleans up the new record; it never borrows the current page.

InNewWindow requests use the canonical retained-space owner until an independent window owner is available. This preserves their profile and live lifetime across opener closure and app suppression. Only InNewDialog continues into the preexisting popup host; its opener-coupled lifetime and nested background routing are known inherited limitations for the later hosting batch. That host is an integration boundary for the later hosting/close batch; there is no new visible Open-in-separate-window or Pop out control. No CEF-style deferred request/success callback is invented for Qt's void adoption API.

## Presentation layout contract

- Shared Colosseum taskbar: app-only projection above; no top tab strip/grouped session chooser
- Per-app retractable sidebar: space rows, native history chooser and later settings/requests; app-specific skins and navigation vocabulary
- Preserve header minus settings; the address row unfolds vertically below it
- Managed UI stays outside the website viewport. Sidebar/URL expansion can change viewport dimensions; outer embedded frame stays stable
- Full-size/restore, website fullscreen and native window placement are separate states. This batch does not implement or claim live-view full-size geometry preservation
- Account History and Downloads remain future full app-page surfaces backed by existing owners; do not confuse them with per-page native traversal
- Reference Preview.qml is a deliberately plain functional harness using local synthetic pages, not the final visual design or a provider frontend

## Open decisions and later seams

Header X (whole app versus current space), playback on switching spaces, request auto-reveal, and revised site-window/platform-dialog placement remain OPEN. This prototype does not make those decisions through defaults in the mock. App minimize/world-switch pause policy is locked but not implemented by these navigation changes. Existing legacy auto-restore preference behavior remains outside this batch; final fresh-start integration must remove that older automatic route rather than present it as approved new behavior.

No changes to production Main.qml/Taskbar presentation, account ownership, download persistence, media backend or WebView2 removal. Later implementations must wire the projection and controls to production shell ownership and preserve the engine-specific adapter boundary.

## Run the functional host

From a matching Qt6.11.1 SDK shell:

```sh
cmake -S tests/feria_foundation_build -B foundation-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build foundation-build --parallel 2
foundation-build/feria_foundation_runtime --preview tests/feria_foundation_probe/Preview.qml
```

The launcher creates disposable settings/account directories and a loopback fixture server. It loads the actual production SessionStore, FeriaAccountStore, FeriaBrowserPolicy, retained deck and WebEngine wrapper. No provider sign-in, external catalogue or production profile is required. QtWebEngine sandbox/security settings must remain enabled.
