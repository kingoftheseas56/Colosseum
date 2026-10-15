# Feria: optional Hemanth testing

Inventory dated 2026-10-06. Implementation baseline: `fd12796da02418363d8359b53282aea7bf30f3a5`.

This covers recent Feria navigation/restoration, reading progress, and D1-D6 media identity/resume/YouTube Music work. It is not an audit of unrelated Colosseum features. Current live-testing priority is QtWebEngine reader and music. WebView2 qualification is deferred and needs its own results later.

Hemanth testing is optional. Agents should perform available automated and runtime checks themselves. A skipped human check does not block otherwise verified work, and does not become a runtime pass. Dots can execute suitable cases in a qualified graphical desktop; that is agent runtime evidence, distinct from Hemanth acceptance.

## Evidence already available

The receiving Windows review recorded a successful application build, eight focused Feria CTest passes, and actual QtWebEngine/WebView2 wrapper plus native-store fixture passes. Wrapper results used injected observations, not live songs or browser seeks. JavaScript fixtures cover reading/MANGA Plus, resume races, connector dispatch and YouTube Music transitions. These results do not establish live-provider compatibility or clean-process resume.

All unchecked cases below mean **no conclusive human/live-runtime result recorded for this baseline**, not a claim that nobody has ever explored that site. Record blocked cases separately from failures. The codec-enabled WebEngine artifact also needs its own qualification; an in-progress build screenshot is not playback evidence.

## Small optional first session

Use a disposable test profile for data-changing cases. Never clear the normal profile to satisfy this checklist.

- [ ] Open a free MANGA Plus chapter in vertical mode, read to a middle page, leave Feria, and use Continue. The same chapter, page and within-page position return.
- [ ] Quit the entire application and repeat that chapter restore. Lazy images load without resetting or drifting the bookmark.
- [ ] Sign in to YouTube Music normally, play an available track past 60 seconds, leave and resume it through Continue. The intended song and time return.
- [ ] Quit the application and repeat music resume. Loading at 0:00 never replaces the saved position.
- [ ] Advance to a second track. Continue and history associate each time with the correct title and artwork; the first track's metadata never labels the second.
- [ ] Open a title from a scrolled shelf or See all, then return with Back. The originating view, selection and scroll position are usable and preserved.

## Navigation and restoration

- [ ] Back from the embedded provider returns to its originating title/search/grid rather than losing that context.
- [ ] In provider fullscreen, Back first exits fullscreen; with host options open, Back returns focus to the app.
- [ ] Title-to-related-title navigation unwinds in order, retaining the original shelf/grid selection.
- [ ] Search opened from Home, title, See all or Apps returns to the correct origin, including focus.
- [ ] Each All/Read/Listen lens retains its appropriate shelf position and provider selection.
- [ ] See all restores selection and scroll after a title/provider round trip; returning Home focuses its originating row link.
- [ ] Delayed artwork or catalogue updates do not repeatedly jump scroll after restoration.
- [ ] User scrolling or changing views cancels stale restoration; rapid Back/re-entry does not restore an older view.
- [ ] Resizing, keyboard navigation and mouse navigation leave the restored selection visible and actionable.
- [ ] A profile switch does not restore the previous profile's navigation or Continue state.

## Reading

- [ ] MANGA Plus saves a chapter-specific entry; different chapters remain distinct despite similar titles.
- [ ] A middle-page bookmark survives additional lazy-loaded pages and loading-title changes without duplicate entries.
- [ ] Exact page plus within-page offset survives a full process restart in vertical mode.
- [ ] A saved page that is not loaded on reopen is reached after lazy loading, without claiming success prematurely.
- [ ] Slow/failed images and an unavailable chapter preserve the last good bookmark and report failure honestly.
- [ ] Reaching the real last page produces sensible completion; a temporarily short scroll surface does not prematurely complete the chapter.
- [ ] Re-reading an earlier page/chapter records the current reading place consistently; no unimplemented furthest-chapter feature is assumed.
- [ ] A free WEBTOON episode exercises the generic long-scroll reader and clean-process restoration.
- [ ] An accessible free VIZ sample exercises its actual reader; opaque/canvas readers do not claim an exact page unless supported.
- [ ] Visible active reading records activity; idle/background time and catalogue/store browsing do not masquerade as reading.
- [ ] Window resizing/image reflow does not silently turn a saved reading place into a substantially different passage/page.
- [ ] Any exposed horizontal/paged reader mode is checked separately; MANGA Plus's dedicated page locator currently targets vertical mode only.

Open HTML novels/articles are still a coverage gap. Native recording currently accepts reading providers `kindle`, `playbooks`, `mangaplus`, `viz`, `webtoon`, `dcui`, and `marvel`; an arbitrary novel URL or WeebCentral is not automatically qualified. EPUB/PDF native-reader coverage belongs to its own implementation checklist, not this QtWebEngine qualification.

## Music and real media

- [ ] YouTube Music sign-in, actual audio output and continued playback work on this WebEngine build. Record login/codec/network failures separately from connector failures.
- [ ] Continue restores the chosen track and time after leaving Feria and after a full restart.
- [ ] The same track opened through different queue/list URLs or changed titles remains one logical Continue item after restart.
- [ ] Distinct track IDs sharing a title remain separate entries.
- [ ] Next/previous, autoplay, shuffle and repeat transitions never attribute one track's time or artwork to another. Queue reconstruction is not implemented or promised.
- [ ] Pause, manual seek, replay and track-end behavior leave sensible Continue/history entries.
- [ ] Uncertain metadata, ad markers and changing player identities preserve the last good bookmark; later coherent data recovers.
- [ ] Available title, artist, album and artwork correspond to the actual selected track; absent metadata is handled without stale values.
- [ ] A controlled unencrypted audio fixture longer than 30 seconds records progress through the real player and resumes after restart.
- [ ] Long podcast/spoken-audio or audiobook fixtures exercise large saved positions, pause and playback-rate changes; a provider tile alone is not podcast support.
- [ ] Multiple players, same-origin embedded media and accessible shadow-root players select the intended active item. Cross-origin inaccessible media is recorded as a boundary, not an inferred pass.
- [ ] Short clips under the current 30-second media threshold do not create misleading resumable entries.

YouTube Music Premium is a pending subscription-provider test; subscription status alone does not establish that its content is non-DRM. Spotify and Apple Music are listed providers but have no equivalent qualified stable-identity connector in this D1-D6 implementation.

## Resume protection: agent-operated real-browser fixtures

These are better automated/driven by an agent than assigned as routine Hemanth labor. Modeled passes already exist; real browser execution remains outstanding.

- [ ] Request 600 seconds before player discovery. Initial 0/20-second samples and delayed metadata cannot overwrite 600 seconds.
- [ ] Real seeks that snap back trigger bounded retries; later coherent observations establish success, not the seek assignment alone.
- [ ] Normal advancement at 1x/2x and another valid rate is handled correctly; saved positions beyond a shortened duration clamp safely.
- [ ] Ad-like waits, deadline expiry and retry exhaustion preserve the bookmark without indefinite seeking or a false success claim.
- [ ] Replaced/reused media elements and same-item reloads cancel stale work while maintaining appropriate protection.
- [ ] Navigation, provider/profile changes and out-of-order callbacks cannot seek or write into the new scope.
- [ ] Explicit App Home/fresh start cancels the old target, including after reload; abandoned restore requests cannot reappear.
- [ ] Slow callbacks still permit coherent progress recording and do not starve capture.

## Account and Continue behavior

- [ ] Existing account data loads without destructive loss; exact legacy YouTube Music aliases collapse in Continue while history remains available.
- [ ] Dismiss from the Continue rail and See all; reopen/restart. Neither an alias nor an older session resurrects the dismissed item.
- [ ] Newer completion suppresses older unfinished aliases after restart.
- [ ] Feria's currently labelled Clear history action requires its confirmation and clears activity while retaining Continue. The current UI calls `clearActivity()`; verify visible wording and results together.
- [ ] Clear activity while a provider remains open: retained progress survives and new activity can still record afterward.
- [ ] Native `clearHistory()` removes activity and retained progress when exercised through a test harness; do not assume the current Feria button invokes it.
- [ ] Recording toggles behave as labelled; switching profiles prevents cross-profile history, bookmarks or pending-resume writes.
- [ ] A representative large disposable history remains responsive when opening Continue, dismissing and clearing activity.

## Recording a result

For each executed case record: case, date, tester (Hemanth/agent), platform, application commit, engine/version, provider/content identifier, expected versus observed behavior, and evidence reference. Keep credentials, cookies, account files and personal screenshots out of Git.

Use **PASS**, **FAIL**, **BLOCKED** or **NOT RUN**. A Hemanth pass is an optional acceptance signal; it does not substitute for automated regression coverage. Linux desktop evidence does not qualify Windows, and a WebView2 result does not qualify QtWebEngine.
