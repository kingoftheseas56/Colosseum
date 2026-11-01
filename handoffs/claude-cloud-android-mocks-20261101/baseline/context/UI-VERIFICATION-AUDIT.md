# Installed beta UI scope and verification audit

Installed source: 9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9. APK SHA256: 98c59b41e0491b26ce3858745b361eca6b233d56d574ce8c4df6fe99ee5a8d64.

Read-only source/evidence review; no device interaction, rerun, app fix, build or publication performed.

Against public alpha 77844eef0bd3b7fee2c1f884be4a76db6d63321d, all 138 changed QML/JS paths are present in the installed APK. 119 of those paths were touched by the known Codex 020/022 task commits. This is a file-overlap count; it does not attribute every changed line in a shared file exclusively to Codex. The other 19 paths are listed separately below.

The complete live flows for the 119-path scope were not qualified. Recorded host tests, representative captures and bounded T20 checks must not be treated as whole-app acceptance. The torrent cancellation/playback tests do not certify the entire torrent sources interface.

| Area | Changes I owned | Commit anchors | Recorded verification and gap |
|---|---|---|---|
| Sources (T5) | Theatre SourcesSheet; comic torrent sources; manga sources; shared SourceCard/SourcePickerList; responsive rows, query/actions and confirmation/cancel presentation. | 9e1f80f3 | Host/representative layout fixtures; later bounded switch tests. Complete real-provider browse/filter/select/inspection/download flows were not qualified. |
| World pages (T1) | Home headers, world/section tabs, featured carousels and actions, poster/book rails, Continue tiles and typography. | 6f20962e; ecc6b416 | Host/representative T20 presentation. Every populated home, scroll restoration and navigation combination was not qualified. |
| Browse/search (T2) | Theatre/Tankoban/Biblio grids, search, genre/category/provider selectors, filter sheets, row indexes; wallpaper search. | 8a072636 | Layout/bounds fixtures. Full live search/filter/pagination/provider flows were not qualified. |
| Libraries/collections (T3) | Phone selection mode, selection marks, card menus, batch actions and Back routing across the three libraries. | f1e55ade; 6cc76d27 | Host/selection/Back tests and representative presentation. Real populated-library and batch-operation flows were not qualified comprehensively. |
| Details/chapter/episode pages (T4) | Movie/series/book/comic/manga detail layout, headers, synopsis, episode/chapter lists, action placement. | 2ff6e0f4 | Host/representative presentation. Complete real-media detail-to-play/read/download flows across all media were not qualified. |
| Settings/account/extensions (T6) | Profile, account welcome/centre/security/recovery/privacy/devices screens, tracker/Stremio panels, Tankoyomi configuration, setup and update layouts; keyboard-aware forms. | 7cbf6239; ecc6b416; 331a0069 | Host/keyboard geometry and bounded physical IME fixtures. Actual sign-in/recovery/sync/security/configuration workflows were not qualified end to end. |
| Generic Universes (T7) | Saga/Shelves pages, generic universe scaffolding, feeds, rails and tiles. | 642dc9a2 | Host/representative presentation. Full real universe navigation/data interactions were not qualified. Bespoke Cosmere/One Piece changes were outside my authored scope. |
| Shared controls | Adaptive breakpoints, font scaling, touch targets, combo boxes, buttons/fields, PhoneModalSheet sizing/reparenting, overlay exclusivity, focus/Back/IME behavior. | 33fd875b;10d0031e;39f2b2dc;bd766dff;b2255042 | Host/fixture tests plus bounded T20 IME checks. Every consumer/state on OnePlus was not qualified. |
| 022 targeted follow-ups | Featured Read routing, dots/Details separation, manga volume alignment, source Back/empty CTA geometry, reader return-to-series and cold Continue label. | 2ff6f605 | Specific T20 checks passed, including synthetic/controlled data seams. This was not full live chapter/content-provider acceptance or whole-app coverage. |

## Additional bundled paths outside the known Codex UI commits

These include player/audio/subtitle controls, comic/ebook reader controls, and bespoke Cosmere/One Piece changes in Claude's lane. They were bundled in the APK I assembled; I did not independently qualify all of their real-app workflows. Ownership boundaries do not remove my responsibility for the combined delivery.

- `qml/AudioMenu.qml`
- `qml/BrowserDrawer.qml`
- `qml/CosmereUniversePage.qml`
- `qml/OnePieceArcCatalogue.qml`
- `qml/OnePieceEastBlueAtlas.qml`
- `qml/PlayerPage.qml`
- `qml/SubStyleBar.qml`
- `qml/SubtitleMenu.qml`
- `qml/comicreader/ComicReaderCommandBar.qml`
- `qml/comicreader/ComicReaderHud.qml`
- `qml/comicreader/ComicReaderImagePopover.qml`
- `qml/comicreader/ComicReaderLayoutPopover.qml`
- `qml/comicreader/ComicReaderPagesOverlay.qml`
- `qml/comicreader/ComicReaderSettingsSheet.qml`
- `qml/comicreader/ComicReaderShell.qml`
- `qml/reader2/AppearancePanel.qml`
- `qml/reader2/Reader2Logic.js`
- `qml/reader2/SearchSheet.qml`
- `qml/reader2/SelectionMenu.qml`

## All 119 Codex-touched changed paths in the installed APK

- `qml/AdaptiveLayout.qml`
- `qml/BiblioBook.qml`
- `qml/BiblioBookRail.qml`
- `qml/BiblioDiscoverPage.qml`
- `qml/BiblioExplorePage.qml`
- `qml/BiblioGenreIndex.qml`
- `qml/BiblioGenrePage.qml`
- `qml/BiblioLibraryPage.qml`
- `qml/BiblioSearch.qml`
- `qml/BiblioWorld.qml`
- `qml/BrowseColumnScaffold.qml`
- `qml/BrowseLayout.qml`
- `qml/BrowseScaffold.qml`
- `qml/CarouselSlide.qml`
- `qml/CollectionCardMenu.qml`
- `qml/CollectionGrid.qml`
- `qml/CollectionMenu.qml`
- `qml/ComicArchiveBoard.qml`
- `qml/ComicArchiveIndex.qml`
- `qml/ComicSeries.qml`
- `qml/ComicSeriesPage.qml`
- `qml/ComicTorrentSourcesPage.qml`
- `qml/ContinueSeeAllPage.qml`
- `qml/DiscoverBrowser.qml`
- `qml/DiscoverPage.qml`
- `qml/DiscoverPicker.qml`
- `qml/DiscoverSidebar.qml`
- `qml/ExtensionsSeeAllPage.qml`
- `qml/ExtensionsSetupSheetAndroid.qml`
- `qml/ExtensionsStorePage.qml`
- `qml/FeaturedCarousel.qml`
- `qml/GenreIndex.qml`
- `qml/GenrePage.qml`
- `qml/LazyPosterShelf.qml`
- `qml/LibraryButton.qml`
- `qml/LibraryPage.qml`
- `qml/LibrarySelectionHost.qml`
- `qml/LocgPublisherPage.qml`
- `qml/Main.qml`
- `qml/MangaChapterSeriesView.qml`
- `qml/MangaReadingRoom.qml`
- `qml/MangaReadingRoomThumbnailMock.qml`
- `qml/MangaSeries.qml`
- `qml/MangaSeriesSharedHeader.qml`
- `qml/MangaTankobanLibrary.qml`
- `qml/MangaTankobanLibraryThumbnailMock.qml`
- `qml/MangaTankobanSourcesPage.qml`
- `qml/MediaDetailScaffold.qml`
- `qml/PhoneActionRow.qml`
- `qml/PhoneChoiceHost.qml`
- `qml/PhoneFilterSheet.qml`
- `qml/PhoneListItem.qml`
- `qml/PhoneModalSheet.qml`
- `qml/PhoneOverlayRegistry.js`
- `qml/PhoneProfilePage.qml`
- `qml/PhoneShellHost.qml`
- `qml/PhoneWorldActions.qml`
- `qml/PhoneWorldTabBar.qml`
- `qml/ReadingDesk.qml`
- `qml/RowIndexSidebar.qml`
- `qml/SagaUniversePage.qml`
- `qml/SearchSurface.qml`
- `qml/SeeMorePill.qml`
- `qml/SettingsScaffold.qml`
- `qml/ShelvesUniversePage.qml`
- `qml/SourceCard.qml`
- `qml/SourcePickerList.qml`
- `qml/SourcesSheet.qml`
- `qml/StremioSyncPanel.qml`
- `qml/TankobanChapterList.qml`
- `qml/TankobanComicsTab.qml`
- `qml/TankobanDiscoverPage.qml`
- `qml/TankobanLibraryTab.qml`
- `qml/TankobanMangaTab.qml`
- `qml/TankobanSeriesView.qml`
- `qml/TankobanWorld.qml`
- `qml/TankoyomiConfigurationPage.qml`
- `qml/TheatreCatalogPage.qml`
- `qml/TheatreGenreIndex.qml`
- `qml/TheatreGenrePage.qml`
- `qml/TheatreRowControls.qml`
- `qml/TheatreSeeAllPage.qml`
- `qml/TheatreSeries.qml`
- `qml/TheatreSeriesView.qml`
- `qml/TheatreWorld.qml`
- `qml/TouchButton.qml`
- `qml/TouchCheckBox.qml`
- `qml/TouchComboBox.qml`
- `qml/TouchMetrics.js`
- `qml/TouchSwitch.qml`
- `qml/TouchTarget.qml`
- `qml/TouchTextField.qml`
- `qml/TrackerSyncCenterPage.qml`
- `qml/UniverseChromeAction.qml`
- `qml/UniverseExtensionPage.qml`
- `qml/UniverseFeed.qml`
- `qml/UniversePosterRail.qml`
- `qml/UniverseScaffold.qml`
- `qml/UniverseShelfViewport.qml`
- `qml/UniverseTile.qml`
- `qml/UpdatePage.qml`
- `qml/ViewportAvoidance.js`
- `qml/WallpaperSearch.qml`
- `qml/WidgetHeader.qml`
- `qml/WorldPage.qml`
- `qml/WorldTabBar.qml`
- `qml/account/AccountButton.qml`
- `qml/account/AccountCenter.qml`
- `qml/account/AccountDataPrivacyPage.qml`
- `qml/account/AccountDevicesPage.qml`
- `qml/account/AccountField.qml`
- `qml/account/AccountFlyout.qml`
- `qml/account/AccountPageFrame.qml`
- `qml/account/AccountProfilePage.qml`
- `qml/account/AccountRecoveryPage.qml`
- `qml/account/AccountSecurityPage.qml`
- `qml/account/AccountWelcome.qml`
- `qml/account/AccountWelcomePhone.qml`
- `qml/account/AccountYourColosseumPage.qml`

The launcher icon restoration (9e896bfd) copies the public alpha resources; this is not a new icon design. Signature/resource checks establish packaging correctness, not application behavior.

Publication remains explicitly held. No remedial UI edits or device tests have been performed during this audit.
