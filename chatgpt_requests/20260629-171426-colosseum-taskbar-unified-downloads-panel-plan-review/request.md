# ChatGPT Mailbox Request: Colosseum taskbar unified downloads panel plan review

Request id: `20260629-171426-colosseum-taskbar-unified-downloads-panel-plan-review`
Repository: `Colosseum`
Mode: `feature-plan-review`

## Hemanth Prompt

Check this Codex request in the GitHub repo and answer in the requested response schema. If you can write files through GitHub, put the reply in `chatgpt_requests/20260629-171426-colosseum-taskbar-unified-downloads-panel-plan-review/response.md`. If not, return the markdown response for Hemanth to paste back into Codex.

## Task

We are planning a new Colosseum taskbar feature.

Feature: add a download icon to the Colosseum taskbar. Clicking it should open a panel/popover showing all downloaded reading media across:
- Tankoban: downloaded comics/manga chapters via the native Downloads / MangaDownloader backbone.
- Biblio: downloaded books via the native Books / BookDownloader backbone.

Please review the attached repo context and propose the cleanest UX + data architecture. This is an advisory planning pass only. Do not write code.

Need answers to:
1. What should the taskbar download panel look like and how should it group books vs comics/manga?
2. What native/QML data API should exist for a unified downloaded-reading-media list?
3. Which existing files/components should be touched, and in what implementation order?
4. What edge cases matter: partial downloads, deleted files, missing metadata/covers, duplicate items, opening readers, progress states?
5. What should Codex verify after implementation?

Important Colosseum rules:
- Reading is download-fed, never stream-fed.
- No fake/sample data in the shipped feature.
- Use existing native download backbones where possible.
- Codex will implement and verify; ChatGPT's role is planning/review only.
- The worktree has unrelated/uncommitted work; do not assume the GitHub HEAD alone is enough. Use the embedded file excerpts and diff in this request as the live context.

## Constraints

- This is an advisory, non-agentic pass.
- Do not assume your response is applied automatically.
- Codex will verify any recommendation before acting.
- Do not ask ChatGPT web automation to scrape or extract output.

## Response Schema

Return exactly:

## Recommended UX
Describe the taskbar icon behavior, panel layout, grouping, empty state, and item actions.

## Data/API Plan
Name the native/QML API shape you recommend, including item fields and signals.

## Implementation Slices
Numbered slices, each with target files and why.

## Edge Cases
Bullets with expected behavior.

## Verification Checklist
Concrete checks Codex should run or smoke.

## Risks / Things To Inspect Locally
Anything ChatGPT cannot know from the packet.

## Repository Context

No context zip was created for this request.

### Git Status

```text
M native/engine/BookDownloader.cpp
 M qml/BiblioWorld.qml
 M qml/Main.qml
 M qml/TheatreSeries.qml
 M resources/book_reader/domains/books/reader/reader_standalone_boot.js
?? tests/
?? wallpapers.ini
```

### Git Diff

```diff
diff --git a/native/engine/BookDownloader.cpp b/native/engine/BookDownloader.cpp
index caff650..717aa63 100644
--- a/native/engine/BookDownloader.cpp
+++ b/native/engine/BookDownloader.cpp
@@ -51,7 +51,14 @@ QString sanitizeFilename(const QString& raw)
     s = s.trimmed();
     while (s.endsWith(QChar('.')) || s.endsWith(QChar(' '))) s.chop(1);
     if (s.isEmpty()) s = QStringLiteral("download");
-    if (s.size() > 200) s = s.left(200);
+    if (s.size() > 200) {
+        const QString suffix = QFileInfo(s).suffix();
+        const QString ext = suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;
+        const int keep = qMax(1, 200 - ext.size());
+        s = s.left(keep).trimmed();
+        while (s.endsWith(QChar('.')) || s.endsWith(QChar(' '))) s.chop(1);
+        s += ext;
+    }
     return s;
 }
 
diff --git a/qml/BiblioWorld.qml b/qml/BiblioWorld.qml
index 575c42a..782827a 100644
--- a/qml/BiblioWorld.qml
+++ b/qml/BiblioWorld.qml
@@ -1,6 +1,5 @@
 // BiblioWorld - the Colosseum world page for books. Owner: A2.
-// Same spine as Tankoban/Theatre, trimmed to the agreed BASE: Featured carousel, Top-10, genres.
-// (No Continue row yet - there's no reading-progress to feed it; that comes "on top" later.)
+// Same spine as Tankoban/Theatre: Featured carousel, Continue, Top-10, genres.
 //
 // Discovery = Apple Books charts via BiblioApi (live, daily-fresh). Catalog.biblio* is the static
 // fallback so the page paints instantly and never sits empty if the live call is slow. Delivery
@@ -42,6 +41,13 @@ WorldPage {
         onSecondaryClicked: (i) => biblio.openByTitle(biblio.featuredRows[i] ? biblio.featuredRows[i].title : "")
     }
 
+    ContinueRow {
+        title: "Continue"
+        items: (Progress.revision, Progress.recent("book", 12))
+        onResumeRequested: (item) => biblio.continueResumeRequested(item)
+        onDetailRequested: (item) => biblio.continueDetailRequested(item)
+    }
+
     TrendingTop10 {
         title: "Top 10 in Biblio"
         items: biblio.topRows
diff --git a/qml/Main.qml b/qml/Main.qml
index 4327845..2c03a8d 100644
--- a/qml/Main.qml
+++ b/qml/Main.qml
@@ -215,6 +215,10 @@ Window {
     // ---- video player: a fullscreen layer over everything; kept alive once opened so mpv
     //      isn't torn down/recreated each play (avoids the use-after-free teardown trap). ----
     property bool playerOpen: false
+    readonly property bool immersiveSurfaceOpen: win.playerOpen
+        || bookReaderLayer.active
+        || (seriesLayer.active && seriesLayer.item && seriesLayer.item.openChapterId.length > 0)
+
     function openPlayer(infoHash, fileIdx, title, backdrop, subType, subId) {
         if (!playerLayer.active) playerLayer.active = true
         win.playerOpen = true
@@ -1000,7 +1004,7 @@ Window {
             item.backRequested.connect(win.closeBook)
             item.minimizeRequested.connect(win.minimizeShell)
             item.closeRequested.connect(function() { Qt.quit() })
-            item.readRequested.connect(win.openBookReader)
+            item.readRequested.connect(win.openBookSession)
         }
     }
 
@@ -1127,6 +1131,9 @@ Window {
     Taskbar {
         id: taskbar
         z: 900
+        visible: !win.immersiveSurfaceOpen
+        enabled: visible
+        onVisibleChanged: if (!visible) open = false
         onSwitchRequested: (id) => Sessions.switchTo(id)
         onCloseRequested: (id) => Sessions.close(id)
         onStartClicked: { /* Start menu is a later spec - placeholder */ }
diff --git a/qml/TheatreSeries.qml b/qml/TheatreSeries.qml
index 032240c..c5206ca 100644
--- a/qml/TheatreSeries.qml
+++ b/qml/TheatreSeries.qml
@@ -1,7 +1,9 @@
 // TheatreSeries - Theatre detail page for movies and series.
 // Mirrors MangaSeries.qml house style: full-bleed banner, inline metadata, reveal gate,
 // pitch-black base, and a slide-up SourcesSheet for Torrentio rows.
+pragma ComponentBehavior: Bound
 import QtQuick
+import QtQuick.Controls
 import "TheatreApi.js" as TheatreApi
 
 Item {
@@ -464,20 +466,56 @@ Item {
                                         anchors.fill: parent
                                         hoverEnabled: true
                                         cursorShape: Qt.PointingHandCursor
-                                        onClicked: page.activeSeason = seasonBtn.modelData
+                                        onClicked: {
+                                            page.activeSeason = seasonBtn.modelData
+                                            episodeList.positionViewAtBeginning()
+                                        }
                                     }
                                 }
                             }
                         }
                     }
 
-                    Repeater {
+                    ListView {
+                        id: episodeList
+                        width: parent.width
+                        height: Math.min(page.episodes.length * rowHeight,
+                                         Math.max(360, page.height - 150))
+                        clip: true
                         model: page.episodes
+                        boundsBehavior: Flickable.StopAtBounds
+                        flickableDirection: Flickable.VerticalFlick
+                        reuseItems: true
+                        cacheBuffer: rowHeight * 6
+                        spacing: 0
+                        property int rowHeight: 92
+
+                        onModelChanged: positionViewAtBeginning()
+
+                        ScrollBar.vertical: ScrollBar {
+                            id: episodeScroll
+                            policy: episodeList.contentHeight > episodeList.height
+                                    ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
+                            width: 8
+                            anchors.right: parent.right
+                            anchors.rightMargin: 18
+                            contentItem: Rectangle {
+                                implicitWidth: 4
+                                radius: 2
+                                color: episodeScroll.active ? theme.gold : Qt.rgba(1, 1, 1, 0.32)
+                            }
+                            background: Rectangle {
+                                implicitWidth: 8
+                                radius: 4
+                                color: Qt.rgba(1, 1, 1, 0.07)
+                            }
+                        }
+
                         delegate: Item {
                             id: ep
                             required property var modelData
-                            width: episodesCol.width
-                            height: 92
+                            width: ListView.view.width
+                            height: episodeList.rowHeight
                             Rectangle {
                                 anchors.fill: parent
                                 color: epMa.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
diff --git a/resources/book_reader/domains/books/reader/reader_standalone_boot.js b/resources/book_reader/domains/books/reader/reader_standalone_boot.js
index 0b180b8..2e30c04 100644
--- a/resources/book_reader/domains/books/reader/reader_standalone_boot.js
+++ b/resources/book_reader/domains/books/reader/reader_standalone_boot.js
@@ -38,7 +38,7 @@
     // Determine format from extension
     var ext = (filePath.split('.').pop() || '').toLowerCase();
     var formatMap = { epub: 'epub', pdf: 'pdf', txt: 'txt', mobi: 'mobi', fb2: 'fb2' };
-    var format = formatMap[ext];
+    var format = formatMap[ext] || (/[\\/]/.test(ext) ? 'epub' : null);
     if (!format) {
       console.error('[ebook-standalone] Unknown format: ' + ext);
       return;
warning: in the working copy of 'native/engine/BookDownloader.cpp', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'qml/BiblioWorld.qml', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'qml/Main.qml', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'qml/TheatreSeries.qml', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'resources/book_reader/domains/books/reader/reader_standalone_boot.js', LF will be replaced by CRLF the next time Git touches it
```

## Selected Files

### qml/Taskbar.qml

```
// Taskbar.qml - the OS-shell's auto-hiding switcher bar.
// The closed Colosseum button and the open taskbar are the same object, so the bar
// grows out of the button instead of swapping between two separate pieces.
import QtQuick
import QtQuick.Layouts

Item {
    id: bar
    anchors.fill: parent

    property var groups: (Sessions.revision, Sessions.groups())
    property string activeId: Sessions.activeId
    property bool open: false
    readonly property int leftEdge: Math.max(18, Math.min(80, parent.width * 0.045))
    readonly property int bottomGap: 16
    readonly property int closedSize: 64

    signal switchRequested(string id)
    signal closeRequested(string id)
    signal startClicked()

    onOpenChanged: if (!open) fan.visible = false

    function groupHasActive(group) {
        var sessions = group.sessions || []
        for (var i = 0; i < sessions.length; i++) {
            if (sessions[i].id === bar.activeId) return true
        }
        return false
    }

    Rectangle {
        id: dock
        x: bar.leftEdge
        y: parent.height - height - bar.bottomGap
        width: bar.open ? Math.min(parent.width - (bar.leftEdge * 2), 1720) : bar.closedSize
        height: bar.closedSize
        radius: bar.open ? 18 : bar.closedSize / 2
        clip: true
        color: startMa.containsMouse || bar.open ? Qt.rgba(0.02, 0.02, 0.04, 0.78)
                                                 : Qt.rgba(0.02, 0.02, 0.03, 0.72)
        border.width: 1
        border.color: startMa.containsMouse ? Qt.rgba(0.94, 0.76, 0.35, 0.56)
                                            : Qt.rgba(1, 1, 1, 0.16)

        Behavior on width { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
        Behavior on radius { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: 140 } }
        Behavior on border.color { ColorAnimation { duration: 140 } }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 14
            spacing: 14

            Item {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                Layout.alignment: Qt.AlignVCenter

                Rectangle {
                    anchors.fill: parent
                    radius: bar.open ? 14 : 24
                    color: startMa.containsMouse ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.055)
                    border.width: bar.open ? 1 : 0
                    border.color: Qt.rgba(1, 1, 1, 0.13)

                    Behavior on radius { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                    Behavior on color { ColorAnimation { duration: 140 } }
                }

                Image {
                    anchors.centerIn: parent
                    width: 28; height: 28
                    source: "../assets/icons/colosseum.svg"
                    fillMode: Image.PreserveAspectFit
                }

                MouseArea {
                    id: startMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.open = !bar.open
                }
            }

            Row {
                Layout.fillWidth: true
                spacing: 10
                opacity: bar.open ? 1 : 0
                enabled: bar.open

                Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

                Repeater {
                    model: bar.groups
                    delegate: Rectangle {
                        id: tile
                        required property var modelData

                        property bool isActive: bar.groupHasActive(modelData)
                        property bool multi: (modelData.sessions || []).length > 1

                        width: tileRow.implicitWidth + 26
                        height: 46
                        radius: 13
                        color: tileMa.containsMouse || tile.isActive ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(1, 1, 1, 0.055)
                        border.width: tile.isActive ? 1 : 0
                        border.color: Qt.rgba(0.94, 0.77, 0.29, 0.85)

                        Row {
                            id: tileRow
                            anchors.centerIn: parent
                            spacing: 9

                            Image {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 20; height: 20
                                source: modelData.icon
                                fillMode: Image.PreserveAspectFit
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.title + (tile.multi ? "  (" + modelData.sessions.length + ")" : "")
                                color: "#f1f1f4"
                                font.pixelSize: 13
                            }
                        }

                        MouseArea {
                            id: tileMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                var sessions = tile.modelData.sessions || []
                                if (sessions.length === 1) {
                                    bar.switchRequested(sessions[0].id)
                                    bar.open = false
                                } else {
                                    fan.openFor(tile, sessions)
                                }
                            }
                        }
                    }
                }
            }

            Row {
                Layout.alignment: Qt.AlignVCenter
                spacing: 12
                opacity: bar.open ? 0.78 : 0
                enabled: bar.open

                Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

                Repeater {
                    model: ["wifi", "bluetooth", "battery"]
                    delegate: Image {
                        required property string modelData
                        anchors.verticalCenter: parent.verticalCenter
                        width: 18; height: 18
                        source: "../assets/icons/" + modelData + ".svg"
                        fillMode: Image.PreserveAspectFit
                    }
                }
            }
        }
    }

    Rectangle {
        id: fan
        property var sessions: []
        width: 292
        visible: false
        height: fanCol.implicitHeight + 16
        radius: 18
        color: Qt.rgba(0.04, 0.04, 0.06, 0.96)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.14)

        function openFor(tile, nextSessions) {
            fan.sessions = nextSessions
            var point = tile.mapToItem(bar, 0, 0)
            fan.x = Math.min(Math.max(bar.leftEdge, point.x), bar.width - fan.width - bar.leftEdge)
            fan.y = dock.y - fan.height - 8
            fan.visible = true
        }

        Column {
            id: fanCol
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4

            Repeater {
                model: fan.sessions
                delegate: Rectangle {
                    required property var modelData
                    width: parent.width
                    height: 40
                    radius: 10
                    color: rowMa.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 60
                        elide: Text.ElideRight
                        text: modelData.title
                        color: "#eaeaef"
                        font.pixelSize: 13
                    }

                    Item {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 24
                        height: 24

                        Rectangle {
                            width: 11; height: 1.4; radius: 1
                            color: closeMa.containsMouse ? "#efc15a" : "#9a9aa4"
                            anchors.centerIn: parent
                            rotation: 45
                        }

                        Rectangle {
                            width: 11; height: 1.4; radius: 1
                            color: closeMa.containsMouse ? "#efc15a" : "#9a9aa4"
                            anchors.centerIn: parent
                            rotation: -45
                        }

                        MouseArea {
                            id: closeMa
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                bar.closeRequested(modelData.id)
                                fan.visible = false
                            }
                        }
                    }

                    MouseArea {
                        id: rowMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            bar.switchRequested(modelData.id)
                            fan.visible = false
                            bar.open = false
                        }
                    }
                }
            }
        }
    }
}

```


### qml/Main.qml

```
// Colosseum — HOME (v1, on the proven spine)
// Fullscreen-exclusive frameless OS surface: persistent wallpaper + frosted-glass chrome.
//   Top bar (clock·pills·system) → Universe hero → unified Continue row → per-medium trending rows.
// Mock data only (no Universe data engine yet). Glass = proven material (see Glass.qml).
// Run:  C:/Qt/6.11.1/mingw_64/bin/qml.exe qml/Main.qml      (Esc / Ctrl+Q to quit)

import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtCore
import "Catalog.js" as Catalog
import "Universes.js" as Universes
import "UniverseApi.js" as UniverseApi
import "McuApi.js" as Mcu
import "TheatreApi.js" as TheatreApi
import "ContinueCovers.js" as ContinueCovers

Window {
    id: win
    visible: true
    visibility: Window.FullScreen
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "#05060a"
    title: "Colosseum"

    property string currentSurface: "Home"
    property string wallpaperSource: "../assets/wallpaper/captured-motion.jpg"

    Settings {
        id: wallpaperSettings
        location: Qt.resolvedUrl("../wallpapers.ini")
        category: "wallpapers"
        property string homePick: ""
        property string tankobanPick: ""
        property string biblioPick: ""
        property string theatrePick: ""
    }

    function wallpaperKey(world) {
        if (world === "Tankoban") return "tankobanPick"
        if (world === "Biblio") return "biblioPick"
        if (world === "Theatre") return "theatrePick"
        return "homePick"
    }
    function parsePick(raw) {
        if (!raw) return null
        if (typeof raw === "object") return raw
        try {
            var parsed = JSON.parse(raw)
            if (typeof parsed === "string") return { image_url: parsed }
            return parsed
        } catch (e) {
            return { image_url: raw }
        }
    }
    function pickFor(world) {
        return parsePick(wallpaperSettings[wallpaperKey(world)])
    }
    function wallpaperWorldForSession(rec) {
        var appType = rec && rec.appType ? ("" + rec.appType).toLowerCase() : ""
        var contentKind = rec && rec.contentKind ? ("" + rec.contentKind).toLowerCase() : ""
        if (appType === "theatre" || contentKind === "movie") return "Theatre"
        if (appType === "tankoban" || contentKind === "comic") return "Tankoban"
        if (appType === "biblio" || contentKind === "book") return "Biblio"
        return currentSurface || "Home"
    }
    function refreshWallpaper() {
        var pick = pickFor(currentSurface)
        wallpaperSource = pick && pick.image_url ? pick.image_url : "../assets/wallpaper/captured-motion.jpg"
    }
    function setWallpaperPick(world, pick) {
        wallpaperSettings[wallpaperKey(world)] = typeof pick === "string" ? pick : JSON.stringify(pick || {})
        if (currentSurface === world) refreshWallpaper()
    }
    function setWallpaperEverywhere(pick) {
        var raw = typeof pick === "string" ? pick : JSON.stringify(pick || {})
        wallpaperSettings.homePick = raw
        wallpaperSettings.tankobanPick = raw
        wallpaperSettings.biblioPick = raw
        wallpaperSettings.theatrePick = raw
        refreshWallpaper()
    }

    Component.onCompleted: refreshWallpaper()

    // Esc: close the series page if open, else leave a world page, else quit. Ctrl+Q always quits.
    Shortcut { sequences: ["Escape"]; onActivated: {
        if (win.playerOpen) win.closePlayer()
        else if (bookReaderLayer.active) win.closeBookReader()
        else if (bookLayer.active) win.closeBook()
        else if (biblioSeriesLayer.active) win.closeBiblioSeries()
        else if (biblioGenreLayer.active) win.closeBiblioGenre()
        else if (searchLayer.active) win.closeSearch()
        else if (worldSearchLayer.active) win.closeWorldSearch()
        else if (theatreSeriesLayer.active) win.closeTheatreSeries()
        else if (seriesLayer.active) win.closeSeries()
        else if (genreLayer.active) win.closeGenre()
        else if (genreIndexLayer.active) win.closeGenreIndex()
        else if (universeLayer.active) win.closeUniverse()
        else if (worldStack.current !== "") win.closeWorld()
        else Qt.quit()
    } }
    Shortcut { sequences: ["Ctrl+Q"]; onActivated: Qt.quit() }

    // Minimize the OS surface to the taskbar — "get it off my screen" WITHOUT quitting (the shell keeps
    // running, art stays warm). A frameless fullscreen window has no normal frame to land in, so when
    // Windows restores it from the taskbar we snap it straight back to fullscreen — never a stray bare
    // rectangle stuck with no titlebar to grab.
    function minimizeShell() { win.showMinimized() }
    onVisibilityChanged: if (win.visibility === Window.Windowed) win.visibility = Window.FullScreen

    // ---- navigation: open a medium's world page over the persistent wallpaper ----
    // Each visited mode keeps ONE live Loader (created on first entry, never destroyed); navigating
    // Home or between modes just toggles visibility. So returning to a mode shows the already-loaded
    // world with its covers INTACT instead of re-downloading them. Real mode pages route to their
    // own QML; unbuilt modes fall back to DemoWorld.qml.
    function worldSourceFor(medium) {
        if (medium === "Tankoban") return "TankobanWorld.qml"
        if (medium === "Theatre") return "TheatreWorld.qml"
        if (medium === "Biblio") return "BiblioWorld.qml"
        return "DemoWorld.qml"
    }
    function openWorld(medium) {
        var found = false
        for (var i = 0; i < openModes.count; i++)
            if (openModes.get(i).mode === medium) { found = true; break }
        if (!found) openModes.append({ mode: medium })   // first visit → create its keep-alive Loader
        worldStack.current = medium
        currentSurface = medium
        refreshWallpaper()
        topbar.visible = false
        page.visible = false
    }
    function closeWorld() {
        worldStack.current = ""                           // hide all worlds; none destroyed
        currentSurface = "Home"
        refreshWallpaper()
        topbar.visible = true
        page.visible = true
    }

    // ---- universe page: a cross-medium destination over the wallpaper, from the home hero ----
    //      The shell picks the right TEMPLATE by the universe's category (anime vs cinematic);
    //      the Loader reloads onto that source, so Marvel opens the CinematicPage, One Piece the
    //      anime UniversePage. ----
    function universeSourceFor(category) {
        return category === "cinematic" ? "CinematicPage.qml" : "UniversePage.qml"
    }
    function openUniverse(name) {
        universeLayer.universeName = name
        universeLayer.universeSource = win.universeSourceFor(Universes.categoryFor(name))
        if (universeLayer.item) universeLayer.item.universeName = name
        universeLayer.active = true
        topbar.visible = false
        page.visible = false
    }
    function closeUniverse() {
        universeLayer.active = false
        topbar.visible = true
        page.visible = true
    }

    function openGenre(name) {
        genreLayer.genreName = name
        if (genreLayer.active && genreLayer.item) genreLayer.item.genreName = name
        else genreLayer.active = true
    }
    function closeGenre() { genreLayer.active = false }

    // ---- genre INDEX (the "Explore" directory of all genres) — a layer below the genre page so a
    //      picked genre opens its GenrePage over the index. Reached from a genre widget's "Explore"
    //      or the genre page's "Explore" pill. ----
    function openGenreIndex() { genreIndexLayer.active = true }
    function closeGenreIndex() { genreIndexLayer.active = false }

    function openBiblioGenre(name) {
        biblioGenreLayer.genreName = name
        if (biblioGenreLayer.active && biblioGenreLayer.item) biblioGenreLayer.item.genreName = name
        else biblioGenreLayer.active = true
    }
    function closeBiblioGenre() { biblioGenreLayer.active = false }

    // ---- series detail: a layer over the current world page (opened from a Top-10 title tile) ----
    function openSeries(title) {
        seriesLayer.resumeSeriesId = ""
        seriesLayer.resumeChapterId = ""
        seriesLayer.title = title
        if (seriesLayer.active && seriesLayer.item) {
            seriesLayer.item.openChapterId = ""        // leave the reader, show the chapter list
            seriesLayer.item.seriesTitle = title
        } else seriesLayer.active = true
    }
    // open a manga series AND jump straight into the reader at a saved chapter (Continue resume).
    function openSeriesAt(title, seriesId, chapterId) {
        seriesLayer.resumeSeriesId = seriesId || ""
        seriesLayer.resumeChapterId = chapterId || ""
        seriesLayer.title = title
        if (seriesLayer.active && seriesLayer.item) {
            seriesLayer.item.seriesTitle = title
            if (seriesId) seriesLayer.item.seriesId = seriesId
            seriesLayer.item.openChapterId = chapterId || ""
        } else seriesLayer.active = true
    }
    function closeSeries() { seriesLayer.active = false }

    // ---- Theatre detail: its own layer (Cinemeta meta + Torrentio sources), parallel to series ----
    function openTheatreSeries(item) {
        theatreSeriesLayer.pendingItem = item
        if (theatreSeriesLayer.active && theatreSeriesLayer.item) theatreSeriesLayer.item.itemData = item
        else theatreSeriesLayer.active = true
    }
    function closeTheatreSeries() { theatreSeriesLayer.active = false }

    // ---- video player: a fullscreen layer over everything; kept alive once opened so mpv
    //      isn't torn down/recreated each play (avoids the use-after-free teardown trap). ----
    property bool playerOpen: false
    readonly property bool immersiveSurfaceOpen: win.playerOpen
        || bookReaderLayer.active
        || (seriesLayer.active && seriesLayer.item && seriesLayer.item.openChapterId.length > 0)

    function openPlayer(infoHash, fileIdx, title, backdrop, subType, subId) {
        if (!playerLayer.active) playerLayer.active = true
        win.playerOpen = true
        // `backdrop` is the poster url; subType/subId (e.g. "movie"/"tt123" or "series"/"tt123:1:2")
        // let the player fetch online subtitles for this exact title/episode.
        playerLayer.item.playTorrent(infoHash, fileIdx, title, backdrop, subType, subId)
    }
    function closePlayer() {
        if (playerLayer.item) playerLayer.item.stop()
        win.playerOpen = false
    }

    // ---- book detail: Biblio's own dust-jacket page, a layer over the world ----
    function openBook(b) {
        bookLayer.book = b
        bookLayer.active = true
    }
    function closeBook() { bookLayer.active = false }

    // ---- the reader: foliate EPUB reader over everything (download-fed, never a stream) ----
    function openBookReader(path, book) {
        if (!path) return
        bookReaderLayer.bookPath = path
        bookReaderLayer.bookMeta = book || ({})
        if (bookReaderLayer.active && bookReaderLayer.item) bookReaderLayer.item.open(path, book || ({}))
        else bookReaderLayer.active = true
    }
    function closeBookReader() { bookReaderLayer.active = false }

    // ---- series detail: Biblio's series page (offline SeriesIndex), a layer over the world ----
    function openBiblioSeries(series, author) {
        biblioSeriesLayer.series = series
        biblioSeriesLayer.author = author || ""
        if (biblioSeriesLayer.active && biblioSeriesLayer.item) {
            biblioSeriesLayer.item.author = biblioSeriesLayer.author
            biblioSeriesLayer.item.series = series
        } else biblioSeriesLayer.active = true
    }
    function closeBiblioSeries() { biblioSeriesLayer.active = false }

    // ---- search: a layer over the world. Biblio has its own rich surface; Tankoban + Theatre use the
    //      generic SearchSurface fed by their own source (AniList / Cinemeta). ----
    function openSearch() {
        var w = worldStack.current
        if (w === "Biblio") { searchLayer.active = true; return }
        if (w === "Tankoban") {
            worldSearchLayer.searchMode = "Tankoban"
            worldSearchLayer.placeholder = "Search manga…"
            worldSearchLayer.active = true
        } else if (w === "Theatre") {
            worldSearchLayer.searchMode = "Theatre"
            worldSearchLayer.placeholder = "Search movies & series…"
            worldSearchLayer.active = true
        }
    }
    function closeSearch() { searchLayer.active = false }
    function closeWorldSearch() { worldSearchLayer.active = false }
    function routeWorldSearchItem(data) {
        win.closeWorldSearch()
        if (worldSearchLayer.searchMode === "Tankoban") win.openSeries(data.title)
        else if (worldSearchLayer.searchMode === "Theatre") win.openTheatreSeries(data)
    }

    function openWallpaperSearch(world) {
        wallpaperLayer.targetWorld = world || currentSurface || "Home"
        wallpaperLayer.active = true
    }
    function closeWallpaperSearch() { wallpaperLayer.active = false }

    // ---- Continue card has TWO actions: the center icon RESUMES into the content; clicking
    //      elsewhere opens the SERIES / DETAIL view. Both use the resume payload each world wrote. ----
    //  resume (center play/read icon):
    function resumeContinue(entry) {
        if (!entry) return
        var r = entry.resume || ({})
        var title = entry.title || entry.caption || ""
        if (entry.kind === "video") {
            if (r.infoHash) win.openMovieSession(r.infoHash, r.fileIdx || 0, title, entry.cover || "")
        } else if (entry.kind === "manga" || entry.kind === "comic") {
            win.openComicSession(title, entry.id || "", r.chapterId || "")
        } else if (entry.kind === "book") {
            if (r.path) win.openBookSession(r.path, r.book ? r.book : entry)
            else win.openBook(r.book ? r.book : entry)
        }
    }
    //  detail (click anywhere else on the card): the series / movie / book page.
    function detailContinue(entry) {
        if (!entry) return
        var title = entry.title || entry.caption || ""
        if (entry.kind === "video") {
            var id = (entry.id || "").split(":")[0]                      // base tt id (strip episode suffix)
            if (id.indexOf("tt") !== 0) { win.resumeContinue(entry); return }   // raw torrent, no Theatre page
            // resolve movie vs series live from Cinemeta (probe series first; a hit → series, else movie),
            // then open the Theatre detail. No stored type needed, so existing entries work too.
            TheatreApi.loadMeta("series", id, function(meta) {
                win.openTheatreSeries({ id: id, type: meta ? "series" : "movie",
                                        title: title, cover: entry.cover || "" })
            })
        } else if (entry.kind === "manga" || entry.kind === "comic") {
            win.openSeries(title)                                        // the series page (chapter list)
        } else if (entry.kind === "book") {
            win.openBook(entry.resume && entry.resume.book ? entry.resume.book : entry)
        }
    }

    // ===== OS-shell session engine (Approach 2: only the active surface is instantiated) =====
    // The UI opens content by registering a SESSION; Sessions.activeChanged then drives the
    // capture -> teardown -> build -> restore switch. contentKind picks the surface.

    // UI entry points (replace direct open* calls from cards / world pages):
    function openMovieSession(infoHash, fileIdx, title, backdrop, subType, subId) {
        Sessions.openOrSwitch({
            "appType": "theatre", "contentKind": "movie", "title": title || "Movie",
            "target": { "infoHash": infoHash, "fileIdx": fileIdx || 0, "title": title || "",
                        "backdrop": backdrop || "", "subType": subType || "", "subId": subId || "" }
        })
    }
    function openComicSession(title, seriesId, chapterId) {
        Sessions.openOrSwitch({
            "appType": "tankoban", "contentKind": "comic", "title": title || "Comic",
            "target": { "title": title || "", "seriesId": seriesId || "", "chapterId": chapterId || "" }
        })
    }
    function openBookSession(path, book) {
        if (!path) return
        var b = book || ({})
        Sessions.openOrSwitch({
            "appType": "biblio", "contentKind": "book", "title": b.title || "Book",
            "target": { "path": path, "book": b, "id": (b.id !== undefined ? ("" + b.id) : path) }
        })
    }

    // dispatcher: build the active surface from a record (+ restore its saved state).
    function activateSession(rec) {
        if (!rec || !rec.id) return
        var t = rec.target || ({})
        var st = rec.savedState || ({})
        currentSurface = wallpaperWorldForSession(rec)
        refreshWallpaper()
        if (rec.contentKind === "movie") {
            if (!playerLayer.active) playerLayer.active = true
            win.playerOpen = true
            playerLayer.item.playTorrent(t.infoHash, t.fileIdx || 0, t.title, t.backdrop, t.subType, t.subId)
            if (playerLayer.item.restoreState) playerLayer.item.restoreState(st)   // precision: Task 5
        } else if (rec.contentKind === "comic") {
            seriesLayer.resumeSeriesId = t.seriesId || ""
            seriesLayer.resumeChapterId = (st.chapterId || t.chapterId || "")
            seriesLayer.title = t.title
            if (seriesLayer.active && seriesLayer.item) {
                seriesLayer.item.seriesTitle = t.title
                if (t.seriesId) seriesLayer.item.seriesId = t.seriesId
                seriesLayer.item.openChapterId = (st.chapterId || t.chapterId || "")
            } else seriesLayer.active = true
            if (seriesLayer.item && seriesLayer.item.restoreState) seriesLayer.item.restoreState(st)  // Task 4
        } else if (rec.contentKind === "book") {
            bookReaderLayer.bookPath = t.path
            bookReaderLayer.bookMeta = t.book || ({})
            if (bookReaderLayer.active && bookReaderLayer.item) bookReaderLayer.item.open(t.path, t.book || ({}))
            else bookReaderLayer.active = true
            // book precision: foliate auto-restores its own CFI on reopen of the same path (Task 6).
        }
    }
    // capture the live outgoing surface's state (called BEFORE teardown).
    function captureSession(rec) {
        if (!rec || !rec.id) return ({})
        if (rec.contentKind === "movie" && playerLayer.item && playerLayer.item.captureState) return playerLayer.item.captureState()
        if (rec.contentKind === "comic" && seriesLayer.item && seriesLayer.item.captureState) return seriesLayer.item.captureState()
        if (rec.contentKind === "book"  && bookReaderLayer.item && bookReaderLayer.item.captureState) return bookReaderLayer.item.captureState()
        return ({})
    }
    // tear the outgoing surface down. Player: stop media but KEEP the mpv host (use-after-free guard).
    function teardownSession(rec) {
        if (!rec || !rec.id) return
        if (rec.contentKind === "movie") {
            if (playerLayer.item) playerLayer.item.stop()
            win.playerOpen = false
        } else if (rec.contentKind === "comic") {
            seriesLayer.active = false
        } else if (rec.contentKind === "book")  {
            bookReaderLayer.active = false
        }
    }

    // ---- design tokens (the skin: glass is the constant; gold is sparing) ----
    Theme { id: theme }

    // ---- bundled editorial serif (the theme's target display face: theme.display = "Fraunces") ----
    FontLoader { source: "../assets/fonts/Fraunces-Regular.ttf" }
    FontLoader { source: "../assets/fonts/Fraunces-Italic.ttf" }

    // =====================================================================
    // BACKDROP — the persistent wallpaper everything composites over.
    // =====================================================================
    Item {
        id: wall
        anchors.fill: parent
        // Real OS wallpaper — a placeholder PICK (Windows 11 "Captured Motion"; its translucent
        // glass-ribbon motif echoes our material, and it's dark enough for the glass to read).
        // Swap from the parked personalization gallery later. Glass composites over WHATEVER sits in
        // `wall`, so the Image "just works" — and it pops against the chrome instead of reading as an app.
        Image {
            anchors.fill: parent
            source: win.wallpaperSource
            fillMode: Image.PreserveAspectCrop
            cache: true
        }
        // gentle global vignette so chrome + text read against the wallpaper, bright or dark
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0,0,0,0.34) }
                GradientStop { position: 0.5; color: Qt.rgba(0,0,0,0.10) }
                GradientStop { position: 1.0; color: Qt.rgba(0,0,0,0.46) }
            }
        }
    }

    // ---- reusable: a clickable row header (the nav-in to a world). Continue isn't a world,
    //      so it opts out with navigable:false (no chevron, no click). ----
    component RowHeader: Item {
        id: rh
        property string title
        property bool navigable: true
        signal clicked()
        implicitWidth: rhRow.implicitWidth
        implicitHeight: rhRow.implicitHeight
        Row {
            id: rhRow
            spacing: 8
            Text {
                text: rh.title
                color: (rh.navigable && rhMa.containsMouse) ? theme.ink : theme.inkDim
                font.family: theme.display; font.pixelSize: 23
            }
            Text {
                text: "›"
                visible: rh.navigable
                color: theme.gold; font.pixelSize: 22
                opacity: rhMa.containsMouse ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }
        }
        MouseArea {
            id: rhMa; anchors.fill: parent
            hoverEnabled: rh.navigable
            cursorShape: rh.navigable ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (rh.navigable) rh.clicked()
        }
    }

    // ---- reusable: a unified Continue card (glass chrome, solid art slot) ----
    component ContinueCard: Glass {
        id: card
        backdrop: wall
        width: 340; height: 148; radius: 14
        signal resumeActivated()   // center play/read icon → resume INTO the content
        signal detailActivated()   // anywhere else on the card → the series / detail view
        property string kind       // "video" → play glyph; "manga"/"comic"/"book" → read glyph
        property string badge
        property string title
        property string sub
        property real progress: 0
        property color art: "#333"
        property string cover: ""
        Row {
            anchors.fill: parent
            Item {   // art slot — real cover over a gradient fallback
                width: 112; height: parent.height
                clip: true
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0; color: Qt.lighter(art, 1.25) }
                        GradientStop { position: 1; color: Qt.darker(art, 1.4) }
                    }
                }
                Image {
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    source: cover
                    visible: cover.length > 0 && status === Image.Ready
                }
            }
            Item {
                width: parent.width - 112; height: parent.height
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 15; spacing: 0
                    Text {
                        text: badge; color: theme.gold
                        font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 1.3
                        Layout.alignment: Qt.AlignLeft
                    }
                    Item { Layout.fillHeight: true }
                    Text { text: title; color: theme.ink; font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold }
                    Text { text: sub; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12; topPadding: 4; bottomPadding: 8 }
                    Rectangle {
                        Layout.fillWidth: true; height: 4; radius: 2; color: Qt.rgba(1,1,1,0.2)
                        Rectangle { width: parent.width * progress; height: parent.height; radius: 2; color: theme.gold }
                    }
                }
            }
        }
        // click ANYWHERE on the card → the series / detail view (the center button below sits on
        // top, so a click on it never falls through to this one)
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: card.detailActivated()
        }
        // the center play / read button, over the cover — resumes INTO the content
        Rectangle {
            id: resumeBtn
            width: 48; height: 48; radius: 24
            x: 56 - width / 2                       // centered on the 112px cover slot
            anchors.verticalCenter: parent.verticalCenter
            color: rbHov.hovered ? Qt.rgba(0,0,0,0.80) : Qt.rgba(0,0,0,0.55)
            border.width: 1.5; border.color: Qt.rgba(1,1,1,0.9)
            scale: rbHov.hovered ? 1.08 : 1.0
            Behavior on scale { NumberAnimation { duration: 130; easing.type: Easing.OutBack } }
            Image {
                anchors.centerIn: parent
                width: card.kind === "video" ? 19 : 22; height: width
                source: card.kind === "video" ? "../assets/icons/play.svg"
                      : card.kind === "book"  ? "../assets/icons/books.svg"
                      : "../assets/icons/manga.svg"
                fillMode: Image.PreserveAspectFit
            }
            HoverHandler { id: rbHov }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: card.resumeActivated() }
        }
    }

    // (PortraitTile · Pill · SysIcon · the top bar now live in shared sibling files:
    //  PortraitTile.qml and TopBar.qml — reused by the world-page template.)

    // =====================================================================
    // FOREGROUND
    // =====================================================================

    // ---- 1. TOP BAR (fixed, glass over wallpaper) — shared shell chrome.
    //      activeMedium "" → HOME: no pill selected (the no-selection rule). Tapping a pill
    //      enters that world. ----
    TopBar {
        id: topbar
        z: 20
        backdrop: wall
        activeMedium: ""
        x: theme.margin; y: 30
        width: win.width - theme.margin * 2
        onMediumSelected: (medium) => win.openWorld(medium)
        onWallpaperClicked: win.openWallpaperSearch("Home")
        onMinimizeClicked: win.minimizeShell()
        onPowerClicked: Qt.quit()
    }

    // ---- pinned top bar is above; everything below SCROLLS (vertical wheel/drag) ----
    Flickable {
        id: page
        z: 0
        anchors.left: parent.left; anchors.right: parent.right
        y: 96
        height: win.height - 96
        contentWidth: width
        contentHeight: contentCol.implicitHeight + 40
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: contentCol
            x: theme.margin
            width: win.width - theme.margin * 2
            topPadding: 10
            spacing: 30

            // ---- 2. UNIVERSE HERO — a real cycling carousel of the universe collection ----
            //      Swipe/drag between universes · dots track + jump · auto-advances. Real banner
            //      key-art (TMDB / AniList, disk-cached). Data: Universes.universes.
            Glass {
                id: hero
                backdrop: wall
                track: page.contentY
                width: parent.width; height: 340; radius: 20
                tint: 0.06

                SwipeView {
                    id: heroView
                    anchors.fill: parent
                    clip: true
                    Repeater {
                        model: Universes.universes
                        delegate: Item {
                            required property var modelData

                            // banner key-art (solid content), rounded to the panel; the IP color
                            // stands in while it loads, then a left-weighted scrim keeps text legible.
                            Rectangle {
                                anchors.fill: parent; radius: hero.radius; clip: true
                                color: modelData.c1 ? modelData.c1 : "#1a1410"
                                Image {
                                    anchors.fill: parent
                                    source: modelData.banner
                                    asynchronous: true; cache: true
                                    fillMode: Image.PreserveAspectCrop
                                    opacity: status === Image.Ready ? 1 : 0
                                    Behavior on opacity { NumberAnimation { duration: 300 } }
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0.0; color: Qt.rgba(0,0,0,0.86) }
                                        GradientStop { position: 0.52; color: Qt.rgba(0,0,0,0.42) }
                                        GradientStop { position: 1.0; color: Qt.rgba(0,0,0,0.06) }
                                    }
                                }
                            }

                            // content (chrome over the art)
                            Column {
                                anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 44
                                spacing: 12
                                Text { text: "UNIVERSE"; color: theme.gold; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 3 }
                                Text { text: modelData.name; color: theme.ink; font.family: theme.display; font.pixelSize: 48 }
                                Text {
                                    text: modelData.blurb
                                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; width: 500; wrapMode: Text.WordWrap
                                }
                                // medium counts as an inline editorial metadata line (bright count · dim
                                // medium) — NOT glass pills, NO gold separators. Transparent "tablet" chips
                                // read cheap over busy banner art (Hemanth, 2026-06-27).
                                Row {
                                    spacing: 22
                                    Repeater {
                                        model: modelData.chips
                                        delegate: Text {
                                            required property var modelData
                                            textFormat: Text.StyledText
                                            font.family: theme.ui; font.pixelSize: 15
                                            text: {
                                                var s = modelData.t
                                                var i = s.indexOf(" ")
                                                var first = i < 0 ? s : s.substring(0, i)
                                                // bold the leading COUNT only; a medium name with no
                                                // number (incl. multi-word like "Graphic Novel") stays
                                                // one uniform weight — never half-bold.
                                                if (!/^\d/.test(first))
                                                    return "<font color='#c9c8d0'>" + s + "</font>"
                                                return "<b><font color='#f7f7f5'>" + first +
                                                       "</font></b> <font color='#c9c8d0'>" + s.substring(i + 1) + "</font>"
                                            }
                                        }
                                    }
                                }
                                Row {
                                    spacing: 12; topPadding: 6
                                    Rectangle {
                                        id: exploreBtn
                                        radius: 12; height: 46; width: exploreRow.implicitWidth + 44
                                        gradient: Gradient {
                                            GradientStop { position: 0; color: exMa.containsMouse ? Qt.rgba(1,1,1,0.23) : Qt.rgba(1,1,1,0.14) }
                                            GradientStop { position: 1; color: exMa.containsMouse ? Qt.rgba(1,1,1,0.10) : Qt.rgba(1,1,1,0.05) }
                                        }
                                        border.width: 1
                                        border.color: exMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.85) : Qt.rgba(1,1,1,0.26)
                                        Behavior on border.color { ColorAnimation { duration: 160 } }
                                        Row {
                                            id: exploreRow; anchors.centerIn: parent; spacing: 10
                                            Text { text: "Explore the universe"; color: theme.ink
                                                font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                                                anchors.verticalCenter: parent.verticalCenter }
                                            Text { text: "→"; color: theme.gold; font.pixelSize: 16
                                                anchors.verticalCenter: parent.verticalCenter
                                                transform: Translate {
                                                    x: exMa.containsMouse ? 3 : 0
                                                    Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                                                }
                                            }
                                        }
                                        MouseArea {
                                            id: exMa; anchors.fill: parent
                                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                            onClicked: win.openUniverse(Universes.universes[heroView.currentIndex].name)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // dots — track the current universe, click to jump (overlay above the SwipeView)
                Row {
                    anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 30
                    spacing: 8; z: 5
                    Repeater {
                        model: Universes.universes.length
                        delegate: Rectangle {
                            required property int index
                            width: index === heroView.currentIndex ? 22 : 8; height: 8; radius: 4
                            color: index === heroView.currentIndex ? theme.gold : Qt.rgba(1,1,1,0.35)
                            Behavior on width { NumberAnimation { duration: 150 } }
                            MouseArea {
                                anchors.fill: parent; anchors.margins: -4
                                cursorShape: Qt.PointingHandCursor
                                onClicked: heroView.currentIndex = index
                            }
                        }
                    }
                }

                // gentle auto-advance through the collection
                Timer {
                    interval: 6500; running: true; repeat: true
                    onTriggered: heroView.currentIndex = (heroView.currentIndex + 1) % Universes.universes.length
                }
            }

            // ---- 3. CONTINUE (one unified row, all mediums mixed; scrolls horizontally) ----
            //      Real resume data from the Progress store; hidden entirely until there's
            //      something to resume. (Naming Progress.revision keeps the binding live.)
            Column {
                id: contCol
                width: parent.width
                spacing: 14
                property var contItems: (Progress.revision, Progress.recent("", 12))
                visible: contItems.length > 0
                function badgeFor(k) {
                    return ({ video: "VIDEO", manga: "MANGA", comic: "COMIC", book: "BOOK" })[k]
                           || (k ? k.toUpperCase() : "")
                }
                RowHeader { title: "Continue"; navigable: false }   // unified resume row, not a world
                Flickable {
                    id: contFlick
                    width: parent.width; height: 148
                    contentWidth: contRow.width; contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: contRow
                        spacing: 18
                        Repeater {
                            model: contCol.contItems
                            delegate: ContinueCard {
                                id: contCard
                                required property var modelData
                                // saved cover, or — for a manga saved without art — an AniList fallback
                                property string resolvedCover: (modelData.cover !== undefined && ("" + modelData.cover).length)
                                                               ? modelData.cover : ""
                                Component.onCompleted: if (!resolvedCover && (modelData.kind === "manga" || modelData.kind === "comic"))
                                    ContinueCovers.fetch(modelData.title || modelData.caption || "", function(u) { contCard.resolvedCover = u })
                                track: page.contentY + contFlick.contentX
                                kind: modelData.kind !== undefined ? modelData.kind : ""
                                badge: contCol.badgeFor(modelData.kind)
                                title: modelData.title !== undefined ? modelData.title : (modelData.caption || "")
                                sub: modelData.sub !== undefined ? modelData.sub : ""
                                cover: resolvedCover
                                progress: modelData.progress !== undefined ? modelData.progress : 0
                                art: modelData.c1 !== undefined ? modelData.c1 : "#333"
                                onResumeActivated: win.resumeContinue(modelData)
                                onDetailActivated: win.detailContinue(modelData)
                            }
                        }
                    }
                }
            }

            // ---- 4. MODE-INTRO WIDGETS — the board that introduces each app AND shows what's inside.
            //      First prototype: Tankoban as a BOOKSHELF (manga covers standing on a shelf ledge).
            //      The other modes get their own widget forms next; this is the shape to react to.
            Bookshelf {
                backdrop: wall
                track: page.contentY
                width: parent.width
                mangaBooks: Catalog.topManga
                comicsBooks: Catalog.topComics
                onClicked: win.openWorld("Tankoban")
                onBookClicked: win.openWorld("Tankoban")
            }

            Item { width: 1; height: 16 }   // bottom breathing room
        }
    }

    // ---- world pages: one keep-alive Loader PER visited mode, stacked over the home on the SAME
    //      wallpaper. worldStack.current picks which is visible; "" = home. Kept alive so covers
    //      don't re-fetch on return (the home's top bar + scroll hide while a world is up). ----
    ListModel { id: openModes }
    Item {
        id: worldStack
        anchors.fill: parent
        property string current: ""                      // "" = home; else the visible mode
        Repeater {
            model: openModes
            delegate: Loader {
                required property string mode
                anchors.fill: parent
                visible: worldStack.current === mode
                active: true
                source: win.worldSourceFor(mode)
                onLoaded: {
                    item.medium = mode
                    item.backdrop = wall
                    item.homeRequested.connect(win.closeWorld)
                    item.mediumSelected.connect(win.openWorld)
                    item.seriesRequested.connect(win.openSeries)
                    item.bookRequested.connect(win.openBook)
                    item.genreRequested.connect(win.openGenre)
                    if (item.genreIndexRequested) item.genreIndexRequested.connect(win.openGenreIndex)
                    var biblioGenreSignal = item["biblio" + "GenreRequested"]
                    if (biblioGenreSignal) biblioGenreSignal.connect(win.openBiblioGenre)
                    if (item.continueResumeRequested) item.continueResumeRequested.connect(win.resumeContinue)
                    if (item.continueDetailRequested) item.continueDetailRequested.connect(win.detailContinue)
                    if (item.wallpaperClicked) item.wallpaperClicked.connect(function() { win.openWallpaperSearch(mode) })
                    if (mode === "Theatre") {
                        var theatreSignal = item["theatre" + "ItemRequested"]
                        if (theatreSignal) theatreSignal.connect(win.openTheatreSeries)
                    }
                    item.searchClicked.connect(win.openSearch)
                    item.minimizeClicked.connect(win.minimizeShell)
                    item.powerClicked.connect(function() { Qt.quit() })
                }
            }
        }
    }

    // ---- universe page layer: opened from the home hero "Explore the universe". Its source is the
    //      per-category template (anime UniversePage / cinematic CinematicPage), chosen in
    //      openUniverse(). Signal sets differ per template, so each optional connect is guarded. ----
    Loader {
        id: universeLayer
        anchors.fill: parent
        z: 40
        active: false
        visible: active
        property string universeName: ""
        property string universeSource: "UniversePage.qml"
        source: universeSource
        onLoaded: {
            item.backdrop = wall
            item.universeName = universeLayer.universeName
            item.backRequested.connect(win.closeUniverse)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            if (item.searchClicked) item.searchClicked.connect(win.openSearch)
            if (item.seriesRequested) item.seriesRequested.connect(win.openSeries)   // anime template only
            if (item.watchRequested) item.watchRequested.connect(win.openTheatreSeries)
        }
    }

    Loader {
        id: genreLayer
        anchors.fill: parent
        z: 45
        active: false
        visible: active
        property string genreName: ""
        source: "GenrePage.qml"
        onLoaded: {
            item.backdrop = wall
            item.genreName = genreLayer.genreName
            item.backRequested.connect(win.closeGenre)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
            item.seriesRequested.connect(win.openSeries)
            item.exploreRequested.connect(function() { win.closeGenre(); win.openGenreIndex() })
        }
    }

    // ---- genre INDEX layer (the "Explore" directory). z below genreLayer so picking a genre opens
    //      its page over the index. ----
    Loader {
        id: genreIndexLayer
        anchors.fill: parent
        z: 44
        active: false
        visible: active
        source: "GenreIndex.qml"
        onLoaded: {
            item.backdrop = wall
            item.backRequested.connect(win.closeGenreIndex)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
            item.genrePicked.connect(win.openGenre)
        }
    }

    Loader {
        id: biblioGenreLayer
        anchors.fill: parent
        z: 46
        active: false
        visible: active
        property string genreName: ""
        source: "BiblioGenrePage.qml"
        onLoaded: {
            item.backdrop = wall
            item.genreName = biblioGenreLayer.genreName
            item.backRequested.connect(win.closeBiblioGenre)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.searchClicked.connect(win.openSearch)
            item.bookRequested.connect(win.openBook)
            item.seriesRequested.connect(win.openBiblioSeries)
        }
    }

    // ---- series detail layer: opened from a Top-10 title tile, sits OVER the world page ----
    Loader {
        id: seriesLayer
        anchors.fill: parent
        z: 50
        active: false
        visible: active
        property string title: ""
        property string resumeSeriesId: ""    // Continue resume: jump straight to this chapter…
        property string resumeChapterId: ""   //   …in this series (set seriesId BEFORE the chapter)
        source: "MangaSeries.qml"
        onLoaded: {
            item.backdrop = wall
            item.seriesTitle = seriesLayer.title
            if (seriesLayer.resumeSeriesId) item.seriesId = seriesLayer.resumeSeriesId
            if (seriesLayer.resumeChapterId) item.openChapterId = seriesLayer.resumeChapterId
            item.backRequested.connect(win.closeSeries)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
        }
    }

    // ---- Theatre detail layer: opened from a Theatre tile, sits OVER the world page ----
    Loader {
        id: theatreSeriesLayer
        anchors.fill: parent
        z: 50
        active: false
        visible: active
        property var pendingItem: ({})
        source: "TheatreSeries.qml"
        onLoaded: {
            item.backdrop = wall
            item.itemData = theatreSeriesLayer.pendingItem
            item.backRequested.connect(win.closeTheatreSeries)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
            item.playRequested.connect(win.openMovieSession)
        }
    }

    // ---- video player layer: above every detail/series layer (mpv under house glass) ----
    Loader {
        id: playerLayer
        anchors.fill: parent
        z: 60
        active: false
        visible: win.playerOpen
        source: "PlayerPage.qml"
        onLoaded: {
            item.backdrop = wall
            item.backRequested.connect(win.closePlayer)
            item.minimizeRequested.connect(win.minimizeShell)
            item.closeRequested.connect(function() { Qt.quit() })
        }
    }

    // ---- book detail layer: Biblio's OWN dust-jacket page over the world (above series) ----
    Loader {
        id: bookLayer
        anchors.fill: parent
        z: 53
        active: false
        visible: active
        property var book: ({})
        source: "BiblioBook.qml"
        onLoaded: {
            item.backdrop = wall
            item.book = bookLayer.book
      
```

[truncated]


### qml/TopBar.qml

```
// TopBar — the shared Colosseum shell chrome: clock/date · library pills · system icons.
// ONE source for the top bar across the home AND every world page.
//   activeMedium == ""   → HOME: no pill is selected (the no-selection rule).
//   activeMedium == "X"  → WORLD: pill X carries the gold selected accent, and a "‹ Home"
//                          affordance appears at the left.
// Emits intent signals; the host (home / world) decides what navigation happens.

import QtQuick

Item {
    id: bar

    required property Item backdrop          // wallpaper to composite the pills' glass over
    property string activeMedium: ""         // "" = home / no selection
    property string clock: "8:29"
    property string ampm: "PM"
    property string date: "Wednesday, June 24"

    signal mediumSelected(string medium)
    signal homeRequested()
    signal searchClicked()
    signal settingsClicked()
    signal wallpaperClicked()
    signal minimizeClicked()
    signal powerClicked()

    implicitHeight: 56

    Theme { id: theme }

    // ---- inline: a system icon button (Image renders the local SVG reliably; tint via opacity) ----
    component SysIcon: Item {
        id: sysRoot
        property url source
        signal clicked()
        width: 22; height: 22
        Image {
            anchors.fill: parent
            source: sysRoot.source
            sourceSize.width: 22; sourceSize.height: 22
            fillMode: Image.PreserveAspectFit
            opacity: sma.containsMouse ? 1.0 : 0.72
        }
        MouseArea {
            id: sma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: sysRoot.clicked()
        }
    }

    // ---- inline: a library pill (selected when its label == activeMedium).
    //      Clean centered TEXT — icons return later with proper active/inactive tinting.
    //      comingSoon → a placeholder mode (e.g. Vinyl): muted "SOON" tag, not navigable. ----
    component Pill: Item {
        id: pill
        property string label
        property url icon
        property bool comingSoon: false
        readonly property bool active: bar.activeMedium === pill.label
        readonly property bool hot: pma.containsMouse && !pill.comingSoon
        implicitWidth: pillContent.implicitWidth + 34
        implicitHeight: 34

        Rectangle {
            anchors.fill: parent; radius: 999
            color: pill.active ? theme.gold : (pill.hot ? theme.glassHi : "transparent")
            border.width: 1
            border.color: pill.active ? "transparent" : (pill.hot ? theme.edge : "transparent")
        }
        Row {
            id: pillContent
            anchors.centerIn: parent
            spacing: 6
            Text {
                text: pill.label
                color: pill.active ? "#1a1408" : (pma.containsMouse && !pill.comingSoon ? theme.ink : theme.inkDim)
                opacity: pill.comingSoon ? 0.6 : 1.0
                font.family: theme.ui; font.pixelSize: 14
                font.weight: pill.active ? Font.DemiBold : Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }
            Rectangle {   // "SOON" marker — placeholder mode, no world yet
                visible: pill.comingSoon
                anchors.verticalCenter: parent.verticalCenter
                radius: 4; height: 15; width: soonText.implicitWidth + 10
                color: Qt.rgba(1,1,1,0.10)
                Text {
                    id: soonText; anchors.centerIn: parent; text: "SOON"
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 8; font.letterSpacing: 0.8
                }
            }
        }
        MouseArea {
            id: pma; anchors.fill: parent
            hoverEnabled: !pill.comingSoon
            cursorShape: pill.comingSoon ? Qt.ArrowCursor : Qt.PointingHandCursor
            onClicked: if (!pill.comingSoon) bar.mediumSelected(pill.label)
        }
    }

    // ---- left: "‹ Home" (world only) + clock/date ----
    Row {
        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
        spacing: 18
        Item {
            visible: bar.activeMedium !== ""
            width: visible ? homeRow.implicitWidth : 0
            height: 34
            anchors.verticalCenter: parent.verticalCenter
            Row {
                id: homeRow; anchors.verticalCenter: parent.verticalCenter; spacing: 5
                Text { text: "‹"; color: hma.containsMouse ? theme.ink : theme.inkDim
                    font.family: theme.ui; font.pixelSize: 22; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Home"; color: hma.containsMouse ? theme.ink : theme.inkDim
                    font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
            }
            MouseArea {
                id: hma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: bar.homeRequested()
            }
        }
        Column {
            spacing: 3
            anchors.verticalCenter: parent.verticalCenter
            Row {
                spacing: 5
                Text { text: bar.clock; color: theme.ink; font.family: theme.display; font.pixelSize: 32 }
                Text { text: bar.ampm; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 16
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 4 }
            }
            Text { text: bar.date; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13 }
        }
    }

    // ---- center: library pills in a glass capsule ----
    Glass {
        backdrop: bar.backdrop
        anchors.centerIn: parent
        radius: 999
        width: pillsRow.implicitWidth + 14; height: 46
        Row {
            id: pillsRow
            anchors.centerIn: parent
            spacing: 4
            // The four modes (Hemanth-locked 2026-06-24). Tankoban = comics+manga · Biblio = books ·
            // Theatre = movies/video · Vinyl = music (placeholder, no world yet).
            Pill { label: "Tankoban" }
            Pill { label: "Biblio" }
            Pill { label: "Theatre" }
            Pill { label: "Vinyl"; comingSoon: true }
        }
    }

    // ---- right: system icons ----
    Row {
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        spacing: 20
        SysIcon { source: "../assets/icons/search.svg";   onClicked: bar.searchClicked() }
        SysIcon { source: "../assets/icons/settings.svg"; onClicked: bar.wallpaperClicked() }
        SysIcon { source: "../assets/icons/minimize.svg"; onClicked: bar.minimizeClicked() }
        SysIcon { source: "../assets/icons/power.svg";    onClicked: bar.powerClicked() }
    }
}

```


### native/SessionStore.h

```
#pragma once

// SessionStore - the OS-shell's open-sessions model, exposed to QML as `Sessions`.
// One small thing: the list of "things you currently have open" (a comic, a movie, a
// book), which one is ACTIVE, and the saved-state blob each carries so it can be torn
// down and rebuilt exactly where you left it (Approach 2 - only the active session is
// ever instantiated). The Taskbar reads this to draw app-grouped tiles; Main.qml's
// switch glue listens to activeChanged to capture/teardown/restore.
//
// QML contract:
//   Sessions.openOrSwitch({appType, contentKind, target, title}) -> id  (dedups by target key)
//   Sessions.switchTo(id)
//   Sessions.close(id)
//   Sessions.saveState(id, obj)   // switch glue writes captured state before teardown
//   QVariantMap Sessions.get(id)  // one record (empty map if not found)
//   QVariantList Sessions.list()  // records in open order
//   QVariantList Sessions.groups()// [{appType,title,icon,sessions:[record,...]}] for the taskbar
//   Sessions.activeId             // "" = none (home shell)
//   Sessions.revision             // bump on every change; name it in a binding to stay reactive
// signals: activeChanged(prevId, nextId)

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>

class SessionStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QString activeId READ activeId NOTIFY activeChangedProp)

public:
    explicit SessionStore(QObject *parent = nullptr) : QObject(parent) {}

    int revision() const { return m_revision; }
    QString activeId() const { return m_activeId; }

    // Open a session for `target`, or switch to it if one already exists (no duplicate).
    // Returns the session id and makes it active. spec: appType drives taskbar grouping,
    // contentKind drives which surface the dispatcher loads, target is the reopen payload.
    Q_INVOKABLE QString openOrSwitch(const QVariantMap &desc) {
        const QString key = targetKey(desc);
        for (const QVariant &v : m_sessions) {
            const QVariantMap rec = v.toMap();
            if (rec.value(QStringLiteral("key")).toString() == key) {
                const QString id = rec.value(QStringLiteral("id")).toString();
                setActive(id);
                return id;
            }
        }

        QVariantMap rec;
        const QString id = QStringLiteral("s%1").arg(++m_idSeq);
        rec.insert(QStringLiteral("id"), id);
        rec.insert(QStringLiteral("key"), key);
        rec.insert(QStringLiteral("appType"), desc.value(QStringLiteral("appType")));
        rec.insert(QStringLiteral("contentKind"), desc.value(QStringLiteral("contentKind")));
        rec.insert(QStringLiteral("title"), desc.value(QStringLiteral("title")));
        rec.insert(QStringLiteral("target"), desc.value(QStringLiteral("target")));
        rec.insert(QStringLiteral("savedState"), QVariantMap());
        m_sessions.append(rec);
        bump();
        setActive(id);
        return id;
    }

    Q_INVOKABLE void switchTo(const QString &id) { setActive(id); }

    Q_INVOKABLE void close(const QString &id) {
        const int idx = indexOf(id);
        if (idx < 0)
            return;

        const bool wasActive = (m_activeId == id);
        m_sessions.removeAt(idx);
        bump();
        if (wasActive) {
            const QString next = m_sessions.isEmpty()
                ? QString()
                : m_sessions.at(qMin(idx, m_sessions.size() - 1))
                      .toMap()
                      .value(QStringLiteral("id"))
                      .toString();
            setActive(next);
        }
    }

    Q_INVOKABLE void saveState(const QString &id, const QVariantMap &state) {
        const int idx = indexOf(id);
        if (idx < 0)
            return;

        QVariantMap rec = m_sessions.at(idx).toMap();
        rec.insert(QStringLiteral("savedState"), state);
        m_sessions[idx] = rec;
        // no bump: saved-state is internal bookkeeping, not a visible change.
    }

    Q_INVOKABLE QVariantMap get(const QString &id) const {
        const int idx = indexOf(id);
        return idx < 0 ? QVariantMap() : m_sessions.at(idx).toMap();
    }

    Q_INVOKABLE QVariantList list() const { return m_sessions; }

    // Group sessions by appType, preserving first-seen order, for the taskbar's tiles.
    Q_INVOKABLE QVariantList groups() const {
        QVariantList out;
        QStringList order;
        for (const QVariant &v : m_sessions) {
            const QVariantMap rec = v.toMap();
            const QString app = rec.value(QStringLiteral("appType")).toString();
            const int gi = order.indexOf(app);
            if (gi < 0) {
                order.append(app);
                QVariantMap group;
                group.insert(QStringLiteral("appType"), app);
                group.insert(QStringLiteral("title"), appTitle(app));
                group.insert(QStringLiteral("icon"), appIcon(app));
                group.insert(QStringLiteral("sessions"), QVariantList{rec});
                out.append(group);
            } else {
                QVariantMap group = out.at(gi).toMap();
                QVariantList sessions = group.value(QStringLiteral("sessions")).toList();
                sessions.append(rec);
                group.insert(QStringLiteral("sessions"), sessions);
                out[gi] = group;
            }
        }
        return out;
    }

    // Env-gated self-test (codebase idiom - see MangaDownloader::selfTest). Logs PASS/FAIL.
    void selfTest() {
        auto mk = [](const QString &app, const QString &kind, const QString &tgt) {
            QVariantMap desc;
            desc.insert(QStringLiteral("appType"), app);
            desc.insert(QStringLiteral("contentKind"), kind);
            desc.insert(QStringLiteral("title"), tgt);
            QVariantMap target;
            target.insert(QStringLiteral("id"), tgt);
            desc.insert(QStringLiteral("target"), target);
            return desc;
        };

        bool ok = true;
        const QString a = openOrSwitch(mk(QStringLiteral("tankoban"),
                                          QStringLiteral("comic"),
                                          QStringLiteral("One Piece")));
        const QString b = openOrSwitch(mk(QStringLiteral("tankoban"),
                                          QStringLiteral("comic"),
                                          QStringLiteral("Berserk")));
        const QString c = openOrSwitch(mk(QStringLiteral("theatre"),
                                          QStringLiteral("movie"),
                                          QStringLiteral("Dune")));
        ok &= (m_sessions.size() == 3);
        const QString aAgain = openOrSwitch(mk(QStringLiteral("tankoban"),
                                               QStringLiteral("comic"),
                                               QStringLiteral("One Piece")));
        ok &= (aAgain == a) && (m_sessions.size() == 3);
        ok &= (m_activeId == a);
        ok &= (groups().size() == 2);
        ok &= (groups().at(0).toMap().value(QStringLiteral("sessions")).toList().size() == 2);
        switchTo(b);
        close(b);
        ok &= (m_activeId == c || m_activeId == a);
        ok &= (m_sessions.size() == 2);
        qInfo("[session-selftest] %s", ok ? "PASS" : "FAIL");
    }

signals:
    void changed();
    void activeChangedProp();
    void activeChanged(const QString &prevId, const QString &nextId);

private:
    static QString appTitle(const QString &app) {
        if (app == QStringLiteral("tankoban"))
            return QStringLiteral("Tankoban");
        if (app == QStringLiteral("theatre"))
            return QStringLiteral("Theatre");
        if (app == QStringLiteral("biblio"))
            return QStringLiteral("Biblio");
        return app;
    }

    static QString appIcon(const QString &app) {
        if (app == QStringLiteral("tankoban"))
            return QStringLiteral("../assets/icons/manga.svg");
        if (app == QStringLiteral("theatre"))
            return QStringLiteral("../assets/icons/play.svg");
        if (app == QStringLiteral("biblio"))
            return QStringLiteral("../assets/icons/books.svg");
        return QString();
    }

    // Stable identity for dedup: appType + contentKind + the target's own id/path/key.
    static QString targetKey(const QVariantMap &desc) {
        const QVariantMap target = desc.value(QStringLiteral("target")).toMap();
        QString tk = target.value(QStringLiteral("id")).toString();
        if (tk.isEmpty())
            tk = target.value(QStringLiteral("path")).toString();
        if (tk.isEmpty())
            tk = target.value(QStringLiteral("infoHash")).toString();
        if (tk.isEmpty())
            tk = desc.value(QStringLiteral("title")).toString();
        return desc.value(QStringLiteral("appType")).toString() + QStringLiteral("\x1f")
             + desc.value(QStringLiteral("contentKind")).toString() + QStringLiteral("\x1f") + tk;
    }

    int indexOf(const QString &id) const {
        for (int i = 0; i < m_sessions.size(); ++i) {
            if (m_sessions.at(i).toMap().value(QStringLiteral("id")).toString() == id)
                return i;
        }
        return -1;
    }

    void setActive(const QString &id) {
        if (m_activeId == id)
            return;
        const QString prev = m_activeId;
        m_activeId = id;
        emit activeChangedProp();
        emit activeChanged(prev, id);
    }

    void bump() {
        ++m_revision;
        emit changed();
    }

    QVariantList m_sessions;
    QString m_activeId;
    int m_revision = 0;
    int m_idSeq = 0;
};

```


### native/main.cpp

```
// Colosseum native launcher. Runs the live qml/ tree with an on-disk HTTP cache
// and the same Metahub IPv4 pin Tankoban-3 uses for instant poster loading.

#include <QDir>
#include <QGuiApplication>
#include <QHash>
#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQmlApplicationEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QtWebEngineQuick/QtWebEngineQuick>
#include <QQmlContext>
#include <QQuickWindow>
#include <qqml.h>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QDebug>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

#include "MangaEngine.h"
#include "ProgressStore.h"
#include "SessionStore.h"
#include "series/seriesindex.h"
#include "engine/MangaDownloader.h"
#include "engine/BookDownloader.h"
#include "reader/BookBridge.h"
#include "player/mpvitem.h"
#include "player/streamserver.h"

class CachingNam : public QNetworkAccessManager {
public:
    CachingNam(QStringList pinnedHosts, QHash<QString, QString> ipv4ByHost, QObject *parent = nullptr)
        : QNetworkAccessManager(parent),
          m_pinnedHosts(std::move(pinnedHosts)),
          m_ipv4ByHost(std::move(ipv4ByHost)) {
        auto *cache = new QNetworkDiskCache(this);
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                            + QStringLiteral("/colosseum-images");
        QDir().mkpath(dir);
        cache->setCacheDirectory(dir);
        cache->setMaximumCacheSize(qint64(1024) * 1024 * 1024);
        setCache(cache);
    }

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &req, QIODevice *outgoing) override {
        QNetworkRequest r(req);
        QUrl u = r.url();
        const QString host = u.host();

        if (m_pinnedHosts.contains(host)) {
            r.setRawHeader("Host", host.toUtf8());
            r.setPeerVerifyName(host);
            r.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

            const QString ipv4 = m_ipv4ByHost.value(host);
            if (!ipv4.isEmpty()) {
                u.setHost(ipv4);
                r.setUrl(u);
            }
        }

        // Respect a User-Agent the caller already set (the QML XHR sets a browser UA for sources
        // like Fandom / MediaWiki that 403 a bot UA); only stamp our own when none was provided.
        if (r.header(QNetworkRequest::UserAgentHeader).isNull())
            r.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Colosseum/0.1"));
        r.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
        r.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        return QNetworkAccessManager::createRequest(op, r, outgoing);
    }

private:
    QStringList m_pinnedHosts;
    QHash<QString, QString> m_ipv4ByHost;
};

class CachingNamFactory : public QQmlNetworkAccessManagerFactory {
public:
    CachingNamFactory(QStringList pinnedHosts, QHash<QString, QString> ipv4ByHost)
        : m_pinnedHosts(std::move(pinnedHosts)),
          m_ipv4ByHost(std::move(ipv4ByHost)) {}

    QNetworkAccessManager *create(QObject *parent) override {
        return new CachingNam(m_pinnedHosts, m_ipv4ByHost, parent);
    }

private:
    QStringList m_pinnedHosts;
    QHash<QString, QString> m_ipv4ByHost;
};

static QString resolveIpv4(const QString &host) {
    const QHostInfo info = QHostInfo::fromName(host);
    for (const QHostAddress &address : info.addresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol)
            return address.toString();
    }
    return {};
}

// Dev-only QML live-reloader: watches the qml/ tree and reloads the root window
// on save, so editing QML feels like Electron's `npm run dev`. Constructed ONLY
// when COLOSSEUM_DEV is set (dev.bat sets it); the normal launcher never makes one.
class QmlReloader : public QObject {
public:
    QmlReloader(QQmlApplicationEngine *engine, const QString &qmlPath, QObject *parent = nullptr)
        : QObject(parent), m_engine(engine) {
        m_qmlPath = QFileInfo(qmlPath).absoluteFilePath();
        m_watchDir = QFileInfo(m_qmlPath).absolutePath();

        m_debounce.setSingleShot(true);
        m_debounce.setInterval(150);  // coalesce an editor's save-burst into one reload
        QObject::connect(&m_debounce, &QTimer::timeout, this, [this] { reload(); });
        QObject::connect(&m_watcher, &QFileSystemWatcher::fileChanged, this,
                         [this](const QString &) { m_debounce.start(); });
        QObject::connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
                         [this](const QString &) { rescan(); m_debounce.start(); });
        rescan();
        qInfo("[dev] live-reload watching %s", qUtf8Printable(m_watchDir));
    }

private:
    // (Re)watch every .qml/.js under the tree. Many editors save via temp-file +
    // rename, which silently drops that file's watch — so we re-add on every pass.
    void rescan() {
        QStringList found;
        QDirIterator it(m_watchDir, {QStringLiteral("*.qml"), QStringLiteral("*.js")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) found << it.next();
        const QStringList watched = m_watcher.files();
        QStringList toAdd;
        for (const QString &p : found)
            if (!watched.contains(p)) toAdd << p;
        if (!toAdd.isEmpty()) m_watcher.addPaths(toAdd);
        if (!m_watcher.directories().contains(m_watchDir)) m_watcher.addPath(m_watchDir);
    }

    void reload() {
        rescan();  // re-arm any watches dropped by atomic saves
        const QList<QObject *> oldRoots = m_engine->rootObjects();
        m_engine->clearComponentCache();
        m_engine->load(QUrl::fromLocalFile(m_qmlPath));
        for (QObject *o : oldRoots) o->deleteLater();  // drop the previous window
        qInfo("[dev] reloaded");
    }

    QQmlApplicationEngine *m_engine;
    QString m_qmlPath;
    QString m_watchDir;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};

int main(int argc, char *argv[]) {
    // mpvqt renders through OpenGL, so the whole Quick scene must use the OpenGL RHI
    // backend (set process-wide, before the QGuiApplication). Proven 2026-06-27 that
    // Colosseum's frosted glass survives this — the player path's one prerequisite.
    // WebEngine (the foliate EPUB reader) also rides OpenGL: share contexts + init it
    // before the QGuiApplication, alongside the RHI pick. All three must precede app.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Colosseum"));

    // The video player surface (mpv), reached from QML as `import Colosseum.Player`.
    qmlRegisterType<MpvItem>("Colosseum.Player", 1, 0, "MpvItem");

    QNetworkProxyFactory::setUseSystemConfiguration(false);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

    QQmlApplicationEngine engine;
    const QString qmlPath = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("qml/Main.qml");
    const QStringList pinnedHosts = {
        QStringLiteral("live.metahub.space"),
        QStringLiteral("images.metahub.space")
    };
    QHash<QString, QString> ipv4ByHost;
    for (const QString &host : pinnedHosts) {
        const QString ipv4 = resolveIpv4(host);
        if (!ipv4.isEmpty())
            ipv4ByHost.insert(host, ipv4);
    }
    engine.setNetworkAccessManagerFactory(new CachingNamFactory(pinnedHosts, ipv4ByHost));

    // Native manga engine (WeebCentral) exposed to QML as `Manga`.
    auto *manga = new MangaEngine(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Manga"), manga);

    // Download-fed reading backbone exposed to QML as `Downloads`. Reading is never
    // a live stream: a chapter is downloaded to loose local files once, then the
    // reader reads those offline. Own plain NAM (no cache) — it persists to disk itself.
    auto *dlNam = new QNetworkAccessManager(&app);
    auto *downloads = new MangaDownloader(dlNam, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("Downloads"), downloads);
    if (qEnvironmentVariableIsSet("COLOSSEUM_DL_SELFTEST"))
        downloads->selfTest(qEnvironmentVariable("COLOSSEUM_DL_SELFTEST"));

    // Book download backbone (LibGen → local .epub) exposed to QML as `Books`.
    // Same download-fed law as manga: a book is fetched to disk once, then the
    // reader opens the local file (never a stream). Shares the plain uncached NAM.
    auto *books = new BookDownloader(dlNam, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("Books"), books);
    if (qEnvironmentVariableIsSet("COLOSSEUM_BOOK_DLTEST"))
        books->selfTest(qEnvironmentVariable("COLOSSEUM_BOOK_DLTEST"));

    // SeriesIndex resolves the offline series DB from the live qml/ tree's sibling tools/ folder.
    const QString qmlDir = QFileInfo(qmlPath).absolutePath();
    const QString seriesDbPath = QDir(QDir(qmlDir).absoluteFilePath(QStringLiteral("..")))
                                     .absoluteFilePath(QStringLiteral("tools/biblio_series.db"));
    auto *seriesIndex = new SeriesIndex(seriesDbPath, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("SeriesIndex"), seriesIndex);
    if (qEnvironmentVariableIsSet("COLOSSEUM_SERIES_SELFTEST"))
        seriesIndex->selfTest();

    // Foliate EPUB reader bridge exposed to the WebEngine reader's QWebChannel as
    // `BookBridge` (a JS shim maps it to window.electronAPI). Ported from TB2.
    auto *bookBridge = new BookBridge(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("BookBridge"), bookBridge);

    // Torrent stream engine (Stremio sidecar) exposed to QML as `Stream`. Lazy: the
    // runtime only spawns on the first Stream.play() call.
    auto *stream = new StreamServer(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Stream"), stream);

    // Continue / resume backbone exposed to QML as `Progress`. The player and the
    // manga reader write watch/read progress; every Continue row reads it back.
    // QSettings-backed, so it survives a restart.
    auto *progress = new ProgressStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Progress"), progress);

    // Open-sessions model exposed to QML as `Sessions` - the OS-shell's switcher state
    // (which surfaces are open, which is active, each one's saved-state blob).
    auto *sessions = new SessionStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Sessions"), sessions);
    if (qEnvironmentVariableIsSet("COLOSSEUM_SESSION_SELFTEST"))
        sessions->selfTest();

    engine.load(QUrl::fromLocalFile(qmlPath));
    if (engine.rootObjects().isEmpty())
        return -1;

    // Live-reload only in dev (dev.bat sets COLOSSEUM_DEV). Production is untouched.
    if (qEnvironmentVariableIsSet("COLOSSEUM_DEV")) {
        new QmlReloader(&engine, qmlPath, &app);
        manga->selfTest(QStringLiteral("Berserk"));  // log WeebCentral chapter count at startup
        manga->volumes(QStringLiteral("One Piece"));  // DEBUG: log MangaDex volume resolution
    }

    return app.exec();
}

```


### native/engine/MangaDownloader.h

```
// MangaDownloader.h
//
// The download-fed backbone: reading is NEVER a live stream. A chapter is
// downloaded once — its page images land as loose files on disk — and the
// reader then reads those local files, offline, forever. This recreates
// Tankoban 2 / the Electron app's proven downloader in Colosseum-lean form:
// the irreducible core (fetch page URLs -> download images -> JSON index ->
// localPages flip) is kept; TB2's CBZ packing / followed-library / history-cap
// are deferred to a later pass (justified up only when needed).
//
// Pipeline (mirrors TB2 + mangaDownloads.js):
//   1. WeebCentralScraper::fetchPages(chapterId)  -> [{index, imageUrl}]
//   2. for each page: GET image -> write <dir>/page_NNN.<ext>  (3 retries,
//      2/4/8s backoff; resume skips existing files > 1 KB; bounded concurrency)
//   3. write an index entry {chapterId -> dir, files[], pageCount, bytes}
//   4. reader calls localPages(chapterId) -> file:/// URLs for the saved pages
//
// On-disk layout (under QStandardPaths::AppDataLocation, NOT the purgeable
// CacheLocation the image cache uses):
//   <appdata>/manga/<series>/<chapter>/page_000.jpg ...
//   <appdata>/manga/index.json
//
// Threading: pure QNetworkAccessManager + QObject lambdas on the main thread.

#pragma once

#include "MangaResult.h"

#include <QObject>
#include <QHash>
#include <QList>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;
class WeebCentralScraper;

class MangaDownloader : public QObject
{
    Q_OBJECT
public:
    // nam is shared with the rest of the app (carries the IPv4-pin / Host fix),
    // so image fetches use the same proven networking the streaming reader did.
    explicit MangaDownloader(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~MangaDownloader() override;

    // ---- QML entry points ----

    // Queue a single chapter for download. Idempotent: an already-downloaded or
    // already-active chapter is a no-op (re-emits finished for the downloaded one).
    Q_INVOKABLE void downloadChapter(const QString& chapterId,
                                     const QString& seriesId,
                                     const QString& seriesTitle,
                                     const QString& chapterLabel);

    // The local-read FLIP. Returns [{index:int, url:"file:///.../page_NNN.ext"}]
    // for a downloaded chapter, or an empty list if it isn't downloaded — the
    // reader shows "go download it" on empty, it NEVER falls back to streaming.
    Q_INVOKABLE QVariantList localPages(const QString& chapterId) const;

    // True once the chapter is on disk with at least one page.
    Q_INVOKABLE bool isDownloaded(const QString& chapterId) const;

    // Live status for binding a row's affordance without waiting for a signal:
    // { state:"none"|"queued"|"downloading"|"done", done:int, total:int }.
    Q_INVOKABLE QVariantMap statusOf(const QString& chapterId) const;

    // Delete a downloaded chapter (loose files + index entry). Emits removed().
    Q_INVOKABLE void deleteChapter(const QString& chapterId);
    // Cancel a queued or in-flight download (aborts replies, drops partials). Emits failed(reason="cancelled").
    Q_INVOKABLE void cancelDownload(const QString& chapterId);

    // Resolve a chapter's THUMBNAIL = its first page. Downloaded -> local file (instant);
    // otherwise scrape the first page once (capped concurrency, cached). Always answers
    // exactly once via thumbReady(chapterId, url) ("" = no thumb).
    Q_INVOKABLE void fetchThumb(const QString& seriesId, const QString& chapterId);

    // Dev smoke (env COLOSSEUM_DL_SELFTEST=<title>): resolve a title -> its earliest
    // chapter -> download it, logging page count + localPages. Proves the whole
    // pipeline headlessly, without driving the GUI. Mirrors MangaEngine::selfTest.
    void selfTest(const QString& seriesTitle);

signals:
    void progress(const QString& chapterId, int done, int total);
    void finished(const QString& chapterId);
    void failed(const QString& chapterId, const QString& reason);
    void removed(const QString& chapterId);
    void thumbReady(const QString& chapterId, const QString& url);

private:
    struct Job {
        QString chapterId;
        QString seriesId;
        QString seriesTitle;
        QString chapterLabel;
        QString dir;                 // resolved chapter directory
        WeebCentralScraper* scraper = nullptr;
        QList<PageInfo> pages;
        QStringList files;           // index-aligned saved filenames ("" until saved)
        int total = 0;
        int done = 0;
        int nextDispatch = 0;        // next page index to GET
        int inFlight = 0;
        qint64 bytes = 0;
        bool failedFlag = false;
        bool cancelled = false;
        QList<QNetworkReply*> replies;   // in-flight image GETs, for cancel/abort
    };

    struct Entry {
        QString seriesId;
        QString seriesTitle;
        QString chapterLabel;
        QString dir;
        QStringList files;
        qint64 bytes = 0;
        qint64 addedAt = 0;
    };

    // queue pump
    void pumpQueue();
    void beginJob(Job* job);
    void onPagesReady(Job* job, const QList<PageInfo>& pages);
    void pumpImages(Job* job);
    void fetchImage(Job* job, int pageIndex, int attempt);
    void onImageSaved(Job* job, int pageIndex, const QString& fileName, qint64 size);
    void failJob(Job* job, const QString& reason);
    void finishJob(Job* job);
    void cleanupJob(Job* job);
    void finalizeCancel(Job* job);   // drop a cancelled job's partials + clean up

    // disk + index
    QString baseDir() const;                       // <appdata>/manga
    QString chapterDir(const QString& seriesId, const QString& chapterId) const;
    static QString safeSeg(const QString& v);      // path-segment sanitiser
    static QString extForContentType(const QString& ct, const QString& fallbackUrl);
    void loadIndex();
    void saveIndex() const;
    void writeEntry(const Job* job);

    QNetworkAccessManager* m_nam = nullptr;
    QHash<QString, Entry> m_index;                 // chapterId -> entry
    QHash<QString, Job*>  m_active;                 // chapterId -> in-flight job
    QQueue<Job*>          m_queue;                  // waiting jobs

    // chapter thumbnails (first-page resolve; capped concurrency, session cache)
    struct ThumbReq { QString seriesId; QString chapterId; };
    QHash<QString, QString> m_thumbCache;           // chapterId -> url ("" = none)
    QSet<QString>           m_thumbInflight;
    QQueue<ThumbReq>        m_thumbQueue;
    int m_thumbActive = 0;
    void pumpThumbs();

    static constexpr int MAX_CONCURRENT_CHAPTERS = 2;
    static constexpr int THUMB_CONCURRENCY       = 3;
    static constexpr int IMAGE_CONCURRENCY       = 3;
    static constexpr int MAX_IMAGE_RETRIES       = 3;
    static constexpr qint64 MIN_VALID_BYTES      = 1024;   // < 1 KB = truncated/placeholder
};

```


### native/engine/MangaDownloader.cpp

```
#include "MangaDownloader.h"
#include "WeebCentralScraper.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QDebug>

// ---------------------------------------------------------------------------
// ctor / dtor
// ---------------------------------------------------------------------------
MangaDownloader::MangaDownloader(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam)
{
    loadIndex();
}

MangaDownloader::~MangaDownloader() = default;

// ---------------------------------------------------------------------------
// disk paths
// ---------------------------------------------------------------------------
QString MangaDownloader::baseDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/manga");
    QDir().mkpath(dir);
    return dir;
}

QString MangaDownloader::safeSeg(const QString& v)
{
    QString s;
    s.reserve(v.size());
    for (const QChar c : v) {
        if (c.isLetterOrNumber() || c == '.' || c == '_' || c == '-')
            s.append(c);
        else
            s.append('_');
    }
    while (s.startsWith('.')) s.remove(0, 1);
    if (s.isEmpty()) s = QStringLiteral("_");
    return s;
}

QString MangaDownloader::chapterDir(const QString& seriesId, const QString& chapterId) const
{
    // chapter segment = readable prefix + short stable hash, so two different
    // chapterIds that sanitise to the same prefix never collide on disk.
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(chapterId.toUtf8(), QCryptographicHash::Sha1).toHex().left(10));
    const QString chapterSeg = safeSeg(chapterId).left(48) + QStringLiteral("-") + hash;
    return baseDir() + QStringLiteral("/") + safeSeg(seriesId) + QStringLiteral("/") + chapterSeg;
}

QString MangaDownloader::extForContentType(const QString& ct, const QString& fallbackUrl)
{
    const QString c = ct.toLower();
    if (c.contains(QLatin1String("jpeg")) || c.contains(QLatin1String("jpg"))) return QStringLiteral("jpg");
    if (c.contains(QLatin1String("png")))  return QStringLiteral("png");
    if (c.contains(QLatin1String("webp"))) return QStringLiteral("webp");
    if (c.contains(QLatin1String("avif"))) return QStringLiteral("avif");
    if (c.contains(QLatin1String("gif")))  return QStringLiteral("gif");
    // fall back to the URL's own suffix
    const QString suffix = QUrl(fallbackUrl).fileName().section('.', -1).toLower();
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")) return QStringLiteral("jpg");
    if (suffix == QLatin1String("png") || suffix == QLatin1String("webp")
        || suffix == QLatin1String("avif") || suffix == QLatin1String("gif")) return suffix;
    return QStringLiteral("jpg");
}

// ---------------------------------------------------------------------------
// index persistence  (<appdata>/manga/index.json)
// ---------------------------------------------------------------------------
void MangaDownloader::loadIndex()
{
    QFile f(baseDir() + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject entries = root.value(QStringLiteral("entries")).toObject();
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.seriesId     = o.value(QStringLiteral("seriesId")).toString();
        e.seriesTitle  = o.value(QStringLiteral("seriesTitle")).toString();
        e.chapterLabel = o.value(QStringLiteral("chapterLabel")).toString();
        e.dir          = o.value(QStringLiteral("dir")).toString();
        e.bytes        = qint64(o.value(QStringLiteral("bytes")).toDouble());
        e.addedAt      = qint64(o.value(QStringLiteral("addedAt")).toDouble());
        for (const QJsonValue v : o.value(QStringLiteral("files")).toArray())
            e.files.append(v.toString());
        if (!e.files.isEmpty())
            m_index.insert(it.key(), e);
    }
    qInfo("[downloads] loaded index: %d chapters", int(m_index.size()));
}

void MangaDownloader::saveIndex() const
{
    QJsonObject entries;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        const Entry& e = it.value();
        QJsonArray files;
        for (const QString& f : e.files) files.append(f);
        entries.insert(it.key(), QJsonObject{
            {QStringLiteral("seriesId"),     e.seriesId},
            {QStringLiteral("seriesTitle"),  e.seriesTitle},
            {QStringLiteral("chapterLabel"), e.chapterLabel},
            {QStringLiteral("dir"),          e.dir},
            {QStringLiteral("bytes"),        double(e.bytes)},
            {QStringLiteral("addedAt"),      double(e.addedAt)},
            {QStringLiteral("files"),        files},
        });
    }
    const QJsonObject root{{QStringLiteral("schemaVersion"), 1},
                           {QStringLiteral("entries"), entries}};
    // atomic write so a crash mid-save never corrupts the index
    QSaveFile f(baseDir() + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.commit();
}

void MangaDownloader::writeEntry(const Job* job)
{
    Entry e;
    e.seriesId     = job->seriesId;
    e.seriesTitle  = job->seriesTitle;
    e.chapterLabel = job->chapterLabel;
    e.dir          = job->dir;
    e.files        = job->files;
    e.bytes        = job->bytes;
    e.addedAt      = QDateTime::currentMSecsSinceEpoch();
    m_index.insert(job->chapterId, e);
    saveIndex();
}

// ---------------------------------------------------------------------------
// QML entry points
// ---------------------------------------------------------------------------
void MangaDownloader::downloadChapter(const QString& chapterId, const QString& seriesId,
                                      const QString& seriesTitle, const QString& chapterLabel)
{
    if (chapterId.isEmpty()) return;
    if (isDownloaded(chapterId)) { emit finished(chapterId); return; }
    if (m_active.contains(chapterId)) return;
    for (const Job* q : m_queue) if (q->chapterId == chapterId) return;   // already queued

    Job* job = new Job;
    job->chapterId    = chapterId;
    job->seriesId     = seriesId;
    job->seriesTitle  = seriesTitle;
    job->chapterLabel = chapterLabel;
    job->dir          = chapterDir(seriesId, chapterId);
    m_queue.enqueue(job);
    emit progress(chapterId, 0, 0);    // surfaces "queued" immediately
    pumpQueue();
}

QVariantList MangaDownloader::localPages(const QString& chapterId) const
{
    QVariantList out;
    const auto it = m_index.constFind(chapterId);
    if (it == m_index.constEnd()) return out;
    const Entry& e = it.value();
    for (int i = 0; i < e.files.size(); ++i) {
        if (e.files[i].isEmpty()) continue;
        out.append(QVariantMap{
            {QStringLiteral("index"), i},
            {QStringLiteral("url"),
             QUrl::fromLocalFile(e.dir + QStringLiteral("/") + e.files[i]).toString()},
            {QStringLiteral("group"), -1}});
    }
    return out;
}

bool MangaDownloader::isDownloaded(const QString& chapterId) const
{
    const auto it = m_index.constFind(chapterId);
    return it != m_index.constEnd() && !it.value().files.isEmpty();
}

QVariantMap MangaDownloader::statusOf(const QString& chapterId) const
{
    if (isDownloaded(chapterId)) {
        const int n = m_index.value(chapterId).files.size();
        return {{QStringLiteral("state"), QStringLiteral("done")},
                {QStringLiteral("done"), n}, {QStringLiteral("total"), n}};
    }
    if (const Job* job = m_active.value(chapterId, nullptr)) {
        const bool started = job->total > 0;
        return {{QStringLiteral("state"),
                 started ? QStringLiteral("downloading") : QStringLiteral("queued")},
                {QStringLiteral("done"), job->done}, {QStringLiteral("total"), job->total}};
    }
    for (const Job* q : m_queue)
        if (q->chapterId == chapterId)
            return {{QStringLiteral("state"), QStringLiteral("queued")},
                    {QStringLiteral("done"), 0}, {QStringLiteral("total"), 0}};
    return {{QStringLiteral("state"), QStringLiteral("none")},
            {QStringLiteral("done"), 0}, {QStringLiteral("total"), 0}};
}

// ---------------------------------------------------------------------------
// delete / cancel
// ---------------------------------------------------------------------------
void MangaDownloader::deleteChapter(const QString& chapterId)
{
    const auto it = m_index.constFind(chapterId);
    if (it == m_index.constEnd()) return;
    const QString dir = it.value().dir;
    if (!dir.isEmpty()) QDir(dir).removeRecursively();
    m_index.remove(chapterId);
    m_thumbCache.remove(chapterId);
    saveIndex();
    qInfo("[downloads] deleted '%s'", qUtf8Printable(chapterId));
    emit removed(chapterId);
}

void MangaDownloader::cancelDownload(const QString& chapterId)
{
    // queued (not yet started) -> drop it from the queue
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i)->chapterId == chapterId) {
            Job* j = m_queue.at(i);
            m_queue.removeAt(i);
            delete j;
            emit removed(chapterId);
            return;
        }
    }
    // in-flight -> flag + abort replies; finalize once all slots have drained
    Job* job = m_active.value(chapterId, nullptr);
    if (!job) return;
    job->cancelled = true;
    const QList<QNetworkReply*> replies = job->replies;
    for (QNetworkReply* r : replies) if (r) r->abort();   // abort -> finished -> cancelled branch
    if (m_active.value(chapterId, nullptr) == job && job->inFlight == 0) finalizeCancel(job);
}

void MangaDownloader::finalizeCancel(Job* job)
{
    if (!job->dir.isEmpty()) QDir(job->dir).removeRecursively();   // drop partials
    const QString id = job->chapterId;
    qInfo("[downloads] cancelled '%s'", qUtf8Printable(id));
    cleanupJob(job);
    emit removed(id);
}

// ---------------------------------------------------------------------------
// chapter thumbnails — first page; downloaded -> local, else scrape once
// (capped concurrency, session cache). Always answers via thumbReady().
// ---------------------------------------------------------------------------
void MangaDownloader::fetchThumb(const QString& seriesId, const QString& chapterId)
{
    if (chapterId.isEmpty()) return;
    if (m_thumbCache.contains(chapterId)) { emit thumbReady(chapterId, m_thumbCache.value(chapterId)); return; }
    if (isDownloaded(chapterId)) {
        const QVariantList lp = localPages(chapterId);
        const QString url = lp.isEmpty() ? QString()
                          : lp.first().toMap().value(QStringLiteral("url")).toString();
        m_thumbCache.insert(chapterId, url);
        emit thumbReady(chapterId, url);
        return;
    }
    if (m_thumbInflight.contains(chapterId)) return;
    for (const ThumbReq& q : m_thumbQueue) if (q.chapterId == chapterId) return;
    m_thumbQueue.enqueue(ThumbReq{seriesId, chapterId});
    pumpThumbs();
}

void MangaDownloader::pumpThumbs()
{
    while (m_thumbActive < THUMB_CONCURRENCY && !m_thumbQueue.isEmpty()) {
        const ThumbReq req = m_thumbQueue.dequeue();
        const QString cid = req.chapterId;
        m_thumbActive++;
        m_thumbInflight.insert(cid);
        auto* sc = new WeebCentralScraper(m_nam, this);
        auto settle = [this, sc, cid](const QString& url) {
            if (!m_thumbInflight.contains(cid)) return;   // already settled by the other signal
            m_thumbCache.insert(cid, url);
            emit thumbReady(cid, url);
            m_thumbInflight.remove(cid);
            m_thumbActive--;
            sc->deleteLater();
            pumpThumbs();
        };
        connect(sc, &MangaScraper::pagesReady, this, [settle](const QList<PageInfo>& pages) {
            settle(pages.isEmpty() ? QString() : pages.first().imageUrl);
        });
        connect(sc, &MangaScraper::errorOccurred, this, [settle](const QString&) { settle(QString()); });
        sc->fetchPages(cid);
    }
}

// ---------------------------------------------------------------------------
// dev smoke — headless end-to-end proof of the pipeline
// ---------------------------------------------------------------------------
void MangaDownloader::selfTest(const QString& seriesTitle)
{
    connect(this, &MangaDownloader::finished, this, [this](const QString& cid) {
        qInfo("[dl-selftest] FINISHED %s -> localPages=%d",
              qUtf8Printable(cid), int(localPages(cid).size()));
    });
    connect(this, &MangaDownloader::failed, this, [](const QString& cid, const QString& reason) {
        qWarning("[dl-selftest] FAILED %s: %s", qUtf8Printable(cid), qUtf8Printable(reason));
    });

    auto* probe = new WeebCentralScraper(m_nam, this);
    connect(probe, &MangaScraper::searchFinished, this,
            [this, probe, seriesTitle](const QList<MangaResult>& r) {
        if (r.isEmpty()) { qWarning("[dl-selftest] '%s': 0 results", qUtf8Printable(seriesTitle)); return; }
        const QString sid = r.first().id;
        connect(probe, &MangaScraper::chaptersReady, this,
                [this, seriesTitle, sid](const QList<ChapterInfo>& ch) {
            if (ch.isEmpty()) { qWarning("[dl-selftest] '%s': 0 chapters", qUtf8Printable(seriesTitle)); return; }
            ChapterInfo pick = ch.first();
            for (const ChapterInfo& c : ch)   // earliest numbered chapter = the smallest download
                if (c.chapterNumber > 0 && (pick.chapterNumber <= 0 || c.chapterNumber < pick.chapterNumber))
                    pick = c;
            const QString label = pick.name.isEmpty()
                ? QStringLiteral("Chapter %1").arg(pick.chapterNumber) : pick.name;
            qInfo("[dl-selftest] %d chapters; downloading '%s' / '%s' (id=%s)",
                  int(ch.size()), qUtf8Printable(seriesTitle), qUtf8Printable(label), qUtf8Printable(pick.id));
            downloadChapter(pick.id, sid, seriesTitle, label);
        });
        probe->fetchChapters(sid);
    });
    probe->search(seriesTitle);
}

// ---------------------------------------------------------------------------
// queue pump + per-job lifecycle
// ---------------------------------------------------------------------------
void MangaDownloader::pumpQueue()
{
    while (m_active.size() < MAX_CONCURRENT_CHAPTERS && !m_queue.isEmpty()) {
        Job* job = m_queue.dequeue();
        m_active.insert(job->chapterId, job);
        beginJob(job);
    }
}

void MangaDownloader::beginJob(Job* job)
{
    QDir().mkpath(job->dir);
    job->scraper = new WeebCentralScraper(m_nam, this);
    connect(job->scraper, &MangaScraper::pagesReady, this,
            [this, job](const QList<PageInfo>& pages) { onPagesReady(job, pages); });
    connect(job->scraper, &MangaScraper::errorOccurred, this,
            [this, job](const QString& e) { failJob(job, e); });
    job->scraper->fetchPages(job->chapterId);
}

void MangaDownloader::onPagesReady(Job* job, const QList<PageInfo>& pages)
{
    if (pages.isEmpty()) { failJob(job, QStringLiteral("no pages found")); return; }

    job->pages = pages;
    job->total = pages.size();
    job->files = QStringList(job->total, QString());

    // resume: count any page already on disk (> 1 KB) from a prior interrupted run
    QDir dir(job->dir);
    for (int i = 0; i < job->total; ++i) {
        const QStringList hits =
            dir.entryList({QStringLiteral("page_%1.*").arg(i, 3, 10, QChar('0'))}, QDir::Files);
        for (const QString& name : hits) {
            const qint64 sz = QFileInfo(dir.filePath(name)).size();
            if (sz > MIN_VALID_BYTES) {
                job->files[i] = name;
                job->done++;
                job->bytes += sz;
                break;
            }
        }
    }

    emit progress(job->chapterId, job->done, job->total);
    if (job->done == job->total) { finishJob(job); return; }
    pumpImages(job);
}

void MangaDownloader::pumpImages(Job* job)
{
    if (job->failedFlag) {
        if (job->inFlight == 0) failJob(job, QStringLiteral("image download failed"));
        return;
    }
    while (job->inFlight < IMAGE_CONCURRENCY && job->nextDispatch < job->total) {
        const int i = job->nextDispatch++;
        if (!job->files[i].isEmpty()) continue;   // resumed page — already on disk
        job->inFlight++;
        fetchImage(job, i, 0);
    }
}

void MangaDownloader::fetchImage(Job* job, int pageIndex, int attempt)
{
    if (job->cancelled) { job->inFlight--; if (job->inFlight == 0) finalizeCancel(job); return; }

    const QString url = job->pages[pageIndex].imageUrl;
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/124.0 Safari/537.36");
    req.setRawHeader("Referer", "https://weebcentral.com/");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);   // we persist to disk ourselves
    req.setTransferTimeout(30000);

    QNetworkReply* reply = m_nam->get(req);
    job->replies.append(reply);
    connect(reply, &QNetworkReply::finished, this, [this, job, pageIndex, attempt, reply]() {
        reply->deleteLater();
        job->replies.removeOne(reply);
        if (job->cancelled) { job->inFlight--; if (job->inFlight == 0) finalizeCancel(job); return; }
        const QByteArray data = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError && data.size() > MIN_VALID_BYTES;

        if (ok) {
            const QString ct =
                reply->header(QNetworkRequest::ContentTypeHeader).toString();
            const QString ext = extForContentType(ct, job->pages[pageIndex].imageUrl);
            const QString name = QStringLiteral("page_%1.%2")
                                     .arg(pageIndex, 3, 10, QChar('0')).arg(ext);
            QSaveFile out(job->dir + QStringLiteral("/") + name);
            if (out.open(QIODevice::WriteOnly) && out.write(data) == data.size() && out.commit()) {
                onImageSaved(job, pageIndex, name, data.size());
                return;
            }
        }

        // failure path: retry with 2/4/8s backoff, then mark the job failed
        if (attempt + 1 < MAX_IMAGE_RETRIES) {
            const int backoffMs = 2000 << attempt;
            QTimer::singleShot(backoffMs, this,
                               [this, job, pageIndex, attempt]() { fetchImage(job, pageIndex, attempt + 1); });
            return;   // slot stays held across the backoff
        }
        qWarning("[downloads] page %d of '%s' failed after %d attempts",
                 pageIndex, qUtf8Printable(job->chapterId), MAX_IMAGE_RETRIES);
        job->failedFlag = true;
        job->inFlight--;
        pumpImages(job);
    });
}

void MangaDownloader::onImageSaved(Job* job, int pageIndex, const QString& fileName, qint64 size)
{
    job->files[pageIndex] = fileName;
    job->done++;
    job->bytes += size;
    job->inFlight--;
    emit progress(job->chapterId, job->done, job->total);
    if (job->done == job->total) { finishJob(job); return; }
    pumpImages(job);
}

void MangaDownloader::finishJob(Job* job)
{
    writeEntry(job);
    qInfo("[downloads] finished '%s' — %d pages, %.1f MB",
          qUtf8Printable(job->chapterId), job->total, double(job->bytes) / (1024.0 * 1024.0));
    const QString id = job->chapterId;
    cleanupJob(job);
    emit finished(id);
}

void MangaDownloader::failJob(Job* job, const QString& reason)
{
    // keep partial files on disk so a re-download resumes instead of restarting
    qWarning("[downloads] FAILED '%s': %s", qUtf8Printable(job->chapterId), qUtf8Printable(reason));
    const QString id = job->chapterId;
    cleanupJob(job);
    emit failed(id, reason);
}

void MangaDownloader::cleanupJob(Job* job)
{
    m_active.remove(job->chapterId);
    if (job->scraper) job->scraper->deleteLater();
    delete job;
    pumpQueue();   // free slot -> start the next queued chapter
}

```


### native/engine/BookDownloader.h

```
// BookDownloader.h
//
// The book half of the download-fed backbone: reading is NEVER a live stream.
// A book is downloaded once — its .epub/.pdf lands as a loose file on disk —
// and the reader opens that local file, offline, forever. This ports Tankoban 2's
// proven BookDownloader (HTTP / LibGen path) into Colosseum-lean form: the
// irreducible core is kept; TB2's magnet/libtorrent transport, MD5-of-bytes
// verification, and cross-mirror failover beyond LibGen are dropped (Colosseum
// has no TorrentClient — its books come from LibGen over HTTP).
//
// Pipeline (mirrors TB2 BookDownloader + LibGenScraper::resolveDownload):
//   1. resolve: GET libgen.li/ads.php?md5=<md5> → parse <a href="get.php?...key=Y">
//      → the ephemeral direct-file URL(s). The key rotates ~60s, so resolve is
//      done immediately before streaming (fresh key = the safe pattern).
//   2. stream: GET the direct URL → write <dir>/<name>.part in chunks (readyRead,
//      NEVER readAll — books can be 100s of MB), stale-key detection on the first
//      chunk (text/html ⇒ key rotated ⇒ failover to next URL), retry 2/4/8s,
//      then atomic .part → final rename.
//   3. index: persist {md5 → path, title, bytes, addedAt} to index.json.
//   4. reader calls localBook(md5) → the on-disk file path, or "" (UI then shows
//      "go download it" — it NEVER falls back to streaming).
//
// On-disk layout (under QStandardPaths::AppDataLocation, NOT the purgeable
// CacheLocation the image cache uses):
//   <appdata>/books/<name>.epub ...
//   <appdata>/books/index.json
//
// Threading: pure QNetworkAccessManager + QObject lambdas on the main thread.

#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

class BookDownloader : public QObject
{
    Q_OBJECT
public:
    // nam is a plain (uncached) NAM owned by the app — book bytes must never be
    // served from the image disk-cache. Mirrors MangaDownloader's dlNam.
    explicit BookDownloader(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~BookDownloader() override;

    // ---- QML entry points ----

    // Resolve a LibGen md5 to its fresh direct URL, then stream it to disk.
    // Idempotent: an already-downloaded md5 re-emits finished() with its path;
    // an already-active/queued md5 is a no-op. `suggestedName` is the filename
    // to save as (e.g. "Dune.epub"); `title` is stored in the index for display.
    Q_INVOKABLE void downloadBook(const QString& md5,
                                  const QString& suggestedName,
                                  const QString& title = QString(),
                                  double expectedBytes = 0);

    // The local-read FLIP. Returns the absolute on-disk path of a downloaded
    // book, or "" if it isn't downloaded — the reader shows "go download it" on
    // empty, it NEVER falls back to streaming.
    Q_INVOKABLE QString localBook(const QString& md5) const;

    // True once the book file is on disk.
    Q_INVOKABLE bool isDownloaded(const QString& md5) const;

    // Live status for binding a row's affordance without waiting for a signal:
    // { state:"none"|"resolving"|"downloading"|"queued"|"done", received, total }.
    Q_INVOKABLE QVariantMap statusOf(const QString& md5) const;

    // Cancel a resolving / in-flight / queued download (aborts, drops partials).
    Q_INVOKABLE void cancelDownload(const QString& md5);

    // Delete a downloaded book (file + index entry). Emits removed().
    Q_INVOKABLE void deleteBook(const QString& md5);

    // Dev smoke (env COLOSSEUM_BOOK_DLTEST=<md5>): resolve + download a book,
    // logging the resolved URL(s) + final path. Proves the whole pipeline
    // headlessly, without driving the GUI. Mirrors MangaDownloader::selfTest.
    void selfTest(const QString& md5);

signals:
    void resolving(const QString& md5);
    void progress(const QString& md5, double received, double total);
    void finished(const QString& md5, const QString& filePath);
    void failed(const QString& md5, const QString& reason);
    void removed(const QString& md5);

private:
    // ── resolve (ads.php → get.php urls) ──
    struct ResolveCtx {
        QString md5;
        QString suggestedName;
        QString title;
        qint64  expectedBytes = 0;
    };
    void onResolveFinished(QNetworkReply* reply);
    QStringList parseResolveHtml(const QByteArray& html) const;

    // ── HTTP streaming download (ported from TB2 BookDownloader, HTTP path) ──
    struct InFlight {
        QString     md5;
        QString     title;
        QStringList urls;          // remaining URLs to try (front = current)
        int         urlIdx = 0;
        int         attempt = 0;   // retry attempt for the current URL (0-based)
        QString     suggestedName;
        qint64      expectedBytes = 0;

        QPointer<QNetworkReply> reply;
        QFile*      file = nullptr;
        QString     partPath;
        QString     finalPath;

        bool        sanityChecked = false;
        qint64      lastProgressEmit = 0;
        qint64      lastProgressBytes = 0;
        qint64      receivedBytes = 0;
    };

    void startDownload(const QString& md5, const QString& title,
                       const QStringList& urls, const QString& suggestedName,
                       qint64 expectedBytes);
    void startAttempt(InFlight& f);
    void onReadyRead();
    void onFinished();
    void onProgressFromReply(qint64 received, qint64 total);
    void retryOrFailover(InFlight& f, const QString& reason);
    void startNextUrlOrFail(InFlight& f);
    void failAndCleanup(InFlight& f, const QString& reason);
    void finalizeSuccess(InFlight& f);
    void closeAndDeletePart(InFlight& f);
    bool detectStaleHtml(const QByteArray& firstChunk, const QString& contentType) const;
    bool pickTargetFilename(InFlight& f);

    bool isActive(const QString& md5) const;

    // ── disk + index ──
    QString baseDir() const;                 // <appdata>/books
    void loadIndex();
    void saveIndex() const;
    void writeEntry(const InFlight& f);

    struct Entry {
        QString path;
        QString title;
        qint64  bytes = 0;
        qint64  addedAt = 0;
    };

    QNetworkAccessManager* m_nam = nullptr;
    QHash<QNetworkReply*, ResolveCtx> m_resolving;   // ads.php fetches in flight
    InFlight*       m_active = nullptr;
    QList<InFlight> m_queue;
    QHash<QString, Entry> m_index;                   // md5 → downloaded entry
};

```


### native/engine/BookDownloader.cpp

```
#include "BookDownloader.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>

namespace {

constexpr const char* kLibGenBase = "https://libgen.li";

// Match LibGen's UA — some CDNs (cdn2.booksdl.lc) flag bare Qt / curl defaults.
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

constexpr qint64 kDiskSpaceSafetyBytes = 50LL * 1024 * 1024;
constexpr int    kProgressThrottleMs    = 500;
constexpr qint64 kProgressThrottleBytes = 512LL * 1024;
constexpr int    kMaxAttempts           = 3;       // per URL, 2/4/8s backoff

int attemptDelayMs(int attempt)
{
    switch (attempt) {
    case 0:  return 0;       // first try immediate
    case 1:  return 2000;
    case 2:  return 4000;
    default: return 8000;
    }
}

QString sanitizeFilename(const QString& raw)
{
    static const QRegularExpression kBadCharRe(
        QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    QString s = raw;
    s.replace(kBadCharRe, QStringLiteral("_"));
    s = s.trimmed();
    while (s.endsWith(QChar('.')) || s.endsWith(QChar(' '))) s.chop(1);
    if (s.isEmpty()) s = QStringLiteral("download");
    if (s.size() > 200) {
        const QString suffix = QFileInfo(s).suffix();
        const QString ext = suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;
        const int keep = qMax(1, 200 - ext.size());
        s = s.left(keep).trimmed();
        while (s.endsWith(QChar('.')) || s.endsWith(QChar(' '))) s.chop(1);
        s += ext;
    }
    return s;
}

QString filenameFromContentDisposition(const QString& cd)
{
    if (cd.isEmpty()) return {};
    static const QRegularExpression kFilenameStarRe(
        QStringLiteral(R"RX(filename\*\s*=\s*(?:UTF-8|utf-8)'[^']*'([^;]+))RX"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kFilenameRe(
        QStringLiteral(R"RX(filename\s*=\s*"([^"]+)")RX"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kFilenameBareRe(
        QStringLiteral(R"RX(filename\s*=\s*([^;]+))RX"),
        QRegularExpression::CaseInsensitiveOption);
    auto m = kFilenameStarRe.match(cd);
    if (m.hasMatch()) return QUrl::fromPercentEncoding(m.captured(1).toLatin1()).trimmed();
    m = kFilenameRe.match(cd);
    if (m.hasMatch()) return m.captured(1).trimmed();
    m = kFilenameBareRe.match(cd);
    if (m.hasMatch()) return m.captured(1).trimmed();
    return {};
}

} // namespace

BookDownloader::BookDownloader(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam)
{
    loadIndex();
}

BookDownloader::~BookDownloader()
{
    if (m_active) {
        closeAndDeletePart(*m_active);
        delete m_active;
        m_active = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// disk + index
// ─────────────────────────────────────────────────────────────────────────────

QString BookDownloader::baseDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/books");
}

void BookDownloader::loadIndex()
{
    QFile f(baseDir() + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.path    = o.value(QStringLiteral("path")).toString();
        e.title   = o.value(QStringLiteral("title")).toString();
        e.bytes   = static_cast<qint64>(o.value(QStringLiteral("bytes")).toDouble());
        e.addedAt = static_cast<qint64>(o.value(QStringLiteral("addedAt")).toDouble());
        // Drop stale entries whose file was deleted outside the app.
        if (!e.path.isEmpty() && QFileInfo::exists(e.path))
            m_index.insert(it.key(), e);
    }
}

void BookDownloader::saveIndex() const
{
    QDir().mkpath(baseDir());
    QJsonObject root;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        QJsonObject o;
        o[QStringLiteral("path")]    = it.value().path;
        o[QStringLiteral("title")]   = it.value().title;
        o[QStringLiteral("bytes")]   = static_cast<double>(it.value().bytes);
        o[QStringLiteral("addedAt")] = static_cast<double>(it.value().addedAt);
        root[it.key()] = o;
    }
    QFile f(baseDir() + QStringLiteral("/index.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void BookDownloader::writeEntry(const InFlight& f)
{
    Entry e;
    e.path    = f.finalPath;
    e.title   = f.title;
    e.bytes   = f.receivedBytes;
    e.addedAt = QDateTime::currentMSecsSinceEpoch();
    m_index.insert(f.md5, e);
    saveIndex();
}

// ─────────────────────────────────────────────────────────────────────────────
// QML entry points
// ─────────────────────────────────────────────────────────────────────────────

QString BookDownloader::localBook(const QString& md5) const
{
    auto it = m_index.constFind(md5.trimmed().toLower());
    if (it == m_index.constEnd()) return {};
    if (!QFileInfo::exists(it.value().path)) return {};
    return it.value().path;
}

bool BookDownloader::isDownloaded(const QString& md5) const
{
    return !localBook(md5).isEmpty();
}

bool BookDownloader::isActive(const QString& md5) const
{
    const QString m = md5.trimmed().toLower();
    if (m_active && m_active->md5 == m) return true;
    for (const InFlight& q : m_queue)
        if (q.md5 == m) return true;
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it)
        if (it.value().md5 == m) return true;
    return false;
}

QVariantMap BookDownloader::statusOf(const QString& md5) const
{
    const QString m = md5.trimmed().toLower();
    QVariantMap s;
    if (isDownloaded(m)) {
        s[QStringLiteral("state")]    = QStringLiteral("done");
        s[QStringLiteral("received")] = static_cast<double>(m_index.value(m).bytes);
        s[QStringLiteral("total")]    = static_cast<double>(m_index.value(m).bytes);
        return s;
    }
    if (m_active && m_active->md5 == m) {
        s[QStringLiteral("state")]    = QStringLiteral("downloading");
        s[QStringLiteral("received")] = static_cast<double>(m_active->receivedBytes);
        s[QStringLiteral("total")]    = static_cast<double>(m_active->expectedBytes);
        return s;
    }
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it) {
        if (it.value().md5 == m) { s[QStringLiteral("state")] = QStringLiteral("resolving"); return s; }
    }
    for (const InFlight& q : m_queue) {
        if (q.md5 == m) { s[QStringLiteral("state")] = QStringLiteral("queued"); return s; }
    }
    s[QStringLiteral("state")] = QStringLiteral("none");
    return s;
}

void BookDownloader::downloadBook(const QString& md5In, const QString& suggestedName,
                                  const QString& title, double expectedBytes)
{
    const QString md5 = md5In.trimmed().toLower();
    if (md5.isEmpty()) { emit failed(md5, QStringLiteral("empty md5")); return; }

    // Idempotent: already on disk → just re-announce it.
    if (isDownloaded(md5)) { emit finished(md5, localBook(md5)); return; }
    // Already resolving / downloading / queued → no-op.
    if (isActive(md5)) return;

    emit resolving(md5);

    // Resolve LibGen's ephemeral get.php URL right before streaming (key ~60s).
    const QUrl target(QStringLiteral("%1/ads.php?md5=%2").arg(QString::fromLatin1(kLibGenBase), md5));
    QNetworkRequest req(target);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_nam->get(req);
    ResolveCtx ctx;
    ctx.md5           = md5;
    ctx.suggestedName = suggestedName.isEmpty() ? (md5 + QStringLiteral(".epub")) : suggestedName;
    ctx.title         = title;
    ctx.expectedBytes = static_cast<qint64>(expectedBytes);
    m_resolving.insert(reply, ctx);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onResolveFinished(reply); });
}

void BookDownloader::onResolveFinished(QNetworkReply* reply)
{
    if (!reply) return;
    const ResolveCtx ctx = m_resolving.take(reply);
    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError err = reply->error();
    const QString errStr = reply->errorString();
    reply->deleteLater();

    if (ctx.md5.isEmpty()) return;  // cancelled

    if (err != QNetworkReply::NoError) {
        emit failed(ctx.md5, QStringLiteral("LibGen /ads.php fetch failed: %1").arg(errStr));
        return;
    }
    const QStringList urls = parseResolveHtml(body);
    if (urls.isEmpty()) {
        emit failed(ctx.md5, QStringLiteral("LibGen /ads.php returned no get.php links"));
        return;
    }
    qInfo() << "[BookDownloader] resolved" << urls.size() << "mirror URL(s) for" << ctx.md5;
    startDownload(ctx.md5, ctx.title, urls, ctx.suggestedName, ctx.expectedBytes);
}

QStringList BookDownloader::parseResolveHtml(const QByteArray& html) const
{
    const QString text = QString::fromUtf8(html);
    // Direct-download link: <a href="get.php?md5=X&key=Y">. RX( )RX delimiters
    // so the embedded )" can't terminate the raw string early.
    static const QRegularExpression kGetRe(
        QStringLiteral(R"RX(<a[^>]*href="(get\.php\?[^"]*md5=[a-fA-F0-9]{32}[^"]*)"[^>]*>)RX"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kLibraryLolRe(
        QStringLiteral(R"RX(<a[^>]*href="(https?://[^"]*library\.lol[^"]+)"[^>]*>)RX"),
        QRegularExpression::CaseInsensitiveOption);

    QStringList urls;
    QSet<QString> seen;
    auto pushUnique = [&](const QString& c) {
        if (c.isEmpty() || seen.contains(c)) return;
        seen.insert(c);
        urls.append(c);
    };

    auto getIt = kGetRe.globalMatch(text);
    while (getIt.hasNext()) {
        QString rel = getIt.next().captured(1);
        rel.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        pushUnique(QStringLiteral("%1/%2").arg(QString::fromLatin1(kLibGenBase), rel));
    }
    auto lolIt = kLibraryLolRe.globalMatch(text);
    while (lolIt.hasNext()) {
        QString url = lolIt.next().captured(1);
        url.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        pushUnique(url);
    }
    return urls;
}

void BookDownloader::cancelDownload(const QString& md5In)
{
    const QString md5 = md5In.trimmed().toLower();
    // Resolving (ads.php in flight)
    for (auto it = m_resolving.begin(); it != m_resolving.end(); ++it) {
        if (it.value().md5 == md5) {
            QNetworkReply* r = it.key();
            m_resolving.erase(it);
            if (r) { r->disconnect(this); r->abort(); r->deleteLater(); }
            emit failed(md5, QStringLiteral("cancelled by user"));
            return;
        }
    }
    // Active stream
    if (m_active && m_active->md5 == md5) {
        failAndCleanup(*m_active, QStringLiteral("cancelled by user"));
        return;
    }
    // Queued
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue[i].md5 == md5) {
            m_queue.removeAt(i);
            emit failed(md5, QStringLiteral("cancelled by user (queued)"));
            return;
        }
    }
}

void BookDownloader::deleteBook(const QString& md5In)
{
    const QString md5 = md5In.trimmed().toLower();
    auto it = m_index.find(md5);
    if (it == m_index.end()) return;
    QFile::remove(it.value().path);
    m_index.erase(it);
    saveIndex();
    emit removed(md5);
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTP streaming download (ported from TB2 BookDownloader, HTTP path)
// ─────────────────────────────────────────────────────────────────────────────

void BookDownloader::startDownload(const QString& md5, const QString& title,
                                   const QStringList& urls, const QString& suggestedName,
                                   qint64 expectedBytes)
{
    InFlight f;
    f.md5           = md5;
    f.title         = title;
    f.urls          = urls;
    f.suggestedName = sanitizeFilename(suggestedName);
    f.expectedBytes = expectedBytes;

    if (m_active) {
        m_queue.append(std::move(f));
        return;
    }
    m_active = new InFlight(std::move(f));
    startAttempt(*m_active);
}

void BookDownloader::startAttempt(InFlight& f)
{
    if (f.urlIdx >= f.urls.size()) {
        failAndCleanup(f, QStringLiteral("all mirror URLs exhausted"));
        return;
    }
    const QString url = f.urls.value(f.urlIdx);
    if (url.isEmpty()) { startNextUrlOrFail(f); return; }

    // Disk-space pre-check when LibGen gave us a usable size.
    if (f.expectedBytes > 0) {
        const QStorageInfo storage(baseDir());
        if (storage.isValid() && storage.isReady()
            && storage.bytesAvailable() < f.expectedBytes + kDiskSpaceSafetyBytes) {
            failAndCleanup(f, QStringLiteral("insufficient disk space for download"));
            return;
        }
    }

    const int delay = attemptDelayMs(f.attempt);
    if (delay <= 0) {
        // issue now
        if (!pickTargetFilename(f)) { failAndCleanup(f, QStringLiteral("could not prepare destination path")); return; }
        f.file = new QFile(f.partPath);
        if (!f.file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QString err = f.file->errorString();
            delete f.file; f.file = nullptr;
            failAndCleanup(f, QStringLiteral("cannot open .part file: %1").arg(err));
            return;
        }
        f.receivedBytes = 0; f.sanityChecked = false;
        f.lastProgressEmit = 0; f.lastProgressBytes = 0;

        QNetworkRequest req{QUrl(url)};
        req.setRawHeader("User-Agent", kUserAgent);
        req.setRawHeader("Accept", "*/*");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* reply = m_nam->get(req);
        f.reply = reply;
        connect(reply, &QNetworkReply::readyRead,        this, &BookDownloader::onReadyRead);
        connect(reply, &QNetworkReply::finished,         this, &BookDownloader::onFinished);
        connect(reply, &QNetworkReply::downloadProgress, this, &BookDownloader::onProgressFromReply);
    } else {
        const QString md5 = f.md5;
        QTimer::singleShot(delay, this, [this, md5]() {
            if (!m_active || m_active->md5 != md5) return;
            // Re-enter with delay already served (attempt 0-path issues the request).
            m_active->attempt = 0;        // collapse to immediate-issue branch
            startAttempt(*m_active);
        });
    }
}

void BookDownloader::onReadyRead()
{
    if (!m_active || !m_active->reply) return;
    InFlight& f = *m_active;
    QNetworkReply* reply = f.reply.data();
    if (!reply) return;

    const QByteArray chunk = reply->readAll();
    if (chunk.isEmpty()) return;

    if (!f.sanityChecked) {
        f.sanityChecked = true;
        const QString ct = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        if (detectStaleHtml(chunk, ct)) {
            qWarning() << "[BookDownloader] stale key for" << f.urls.value(f.urlIdx)
                       << "(Content-Type=" << ct << ") — failing over";
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
            f.reply.clear();
            if (f.file) { f.file->close(); f.file->remove(); delete f.file; f.file = nullptr; }
            startNextUrlOrFail(f);   // stale key is URL-level, skip this URL's retries
            return;
        }
        // Honour a safe Content-Disposition filename (only finalPath; partPath
        // stays in sync with the already-open QFile, renamed at finalize).
        const QString cd = reply->header(QNetworkRequest::ContentDispositionHeader).toString();
        const QString cdName = filenameFromContentDisposition(cd);
        if (!cdName.isEmpty()) {
            const QString sane = sanitizeFilename(cdName);
            if (!sane.isEmpty()) f.finalPath = QDir(baseDir()).absoluteFilePath(sane);
        }
    }

    if (f.file) {
        const qint64 written = f.file->write(chunk);
        if (written < 0) { failAndCleanup(f, QStringLiteral("disk write failed: %1").arg(f.file->errorString())); return; }
        f.receivedBytes += written;
    }
}

void BookDownloader::onProgressFromReply(qint64 received, qint64 total)
{
    if (!m_active || !m_active->reply) return;
    InFlight& f = *m_active;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsedMs = (f.lastProgressEmit == 0) ? (kProgressThrottleMs + 1)
                                                       : (nowMs - f.lastProgressEmit);
    const qint64 deltaBytes = received - f.lastProgressBytes;
    if (elapsedMs >= kProgressThrottleMs || deltaBytes >= kProgressThrottleBytes) {
        f.lastProgressEmit = nowMs;
        f.lastProgressBytes = received;
        emit progress(f.md5, static_cast<double>(received), static_cast<double>(total));
    }
}

void BookDownloader::onFinished()
{
    if (!m_active || !m_active->reply) return;
    InFlight& f = *m_active;
    QNetworkReply* reply = f.reply.data();
    if (!reply) return;

    const QNetworkReply::NetworkError err = reply->error();
    const QString errString = reply->errorString();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (err == QNetworkReply::NoError) {
        const QByteArray tail = reply->readAll();
        if (!tail.isEmpty() && f.file) { f.file->write(tail); f.receivedBytes += tail.size(); }
    }
    reply->deleteLater();
    f.reply.clear();

    if (err != QNetworkReply::NoError) {
        qWarning() << "[BookDownloader] reply error" << err << "http=" << httpStatus << errString;
        retryOrFailover(f, QStringLiteral("HTTP error: %1 (status %2)").arg(errString).arg(httpStatus));
        return;
    }
    emit progress(f.md5, static_cast<double>(f.receivedBytes), static_cast<double>(f.receivedBytes));
    finalizeSuccess(f);
}

void BookDownloader::finalizeSuccess(InFlight& f)
{
    if (f.file) { f.file->close(); delete f.file; f.file = nullptr; }

    if (f.receivedBytes <= 0) {
        QFile::remove(f.partPath);
        failAndCleanup(f, QStringLiteral("server returned empty body"));
        return;
    }
    if (QFile::exists(f.finalPath)) QFile::remove(f.finalPath);
    if (!QFile::rename(f.partPath, f.finalPath)) {
        const QString reason = QStringLiteral("rename %1 -> %2 failed").arg(f.partPath, f.finalPath);
        QFile::remove(f.partPath);
        failAndCleanup(f, reason);
        return;
    }

    const QString md5 = f.md5;
    const QString finalPath = f.finalPath;
    qInfo() << "[BookDownloader] complete md5=" << md5 << "path=" << finalPath
            << "bytes=" << f.receivedBytes;

    writeEntry(f);
    emit finished(md5, finalPath);

    delete m_active; m_active = nullptr;
    if (!m_queue.isEmpty()) {
        m_active = new InFlight(std::move(m_queue.takeFirst()));
        startAttempt(*m_active);
    }
}

void BookDownloader::retryOrFailover(InFlight& f, const QString& reason)
{
    closeAndDeletePart(f);
    f.attempt += 1;
    if (f.attempt < kMaxAttempts) { startAttempt(f); return; }
    qInfo() << "[BookDownloader] url exhausted, failover:" << reason;
    startNextUrlOrFail(f);
}

void BookDownloader::startNextUrlOrFail(InFlight& f)
{
    f.urlIdx += 1;
    f.attempt = 0;
    if (f.urlIdx >= f.urls.size()) { failAndCleanup(f, QStringLiteral("all mirror URLs failed")); return; }
    startAttempt(f);
}

void BookDownloader::failAndCleanup(InFlight& f, const QString& reason)
{
    closeAndDeletePart(f);
    const QString md5 = f.md5;
    emit failed(md5, reason);
    delete m_active; m_active = nullptr;
    if (!m_queue.isEmpty()) {
        m_active = new InFlight(std::move(m_queue.takeFirst()));
        startAttempt(*m_active);
    }
}

void BookDownloader::closeAndDeletePart(InFlight& f)
{
    if (f.reply) {
        QNetworkReply* r = f.reply.data();
        if (r) { r->disconnect(this); r->abort(); r->deleteLater(); }
        f.reply.clear();
    }
    if (f.file) {
        f.file->close();
        const QString path = f.file->fileName();
        delete f.file; f.file = nullptr;
        QFile::remove(path);
    } else if (!f.partPath.isEmpty() && QFile::exists(f.partPath)) {
        QFile::remove(f.partPath);
    }
}

bool BookDownloader::detectStaleHtml(const QByteArray& firstChunk, const QString& contentType) const
{
    if (contentType.contains(QStringLiteral("text/html"), Qt::CaseInsensitive)) return true;
    if (firstChunk.size() >= 5) {
        const QByteArray head = firstChunk.left(512).trimmed().toLower();
        if (head.startsWith("<!doctype html") || head.startsWith("<html") || head.startsWith("<!doctype"))
            return true;
    }
    return false;
}

bool BookDownloader::pickTargetFilename(InFlight& f)
{
    QDir dir(baseDir());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        qWarning() << "[BookDownloader] mkpath failed for" << baseDir();
        return false;
    }
    QString chosen = f.suggestedName;
    if (chosen.isEmpty()) chosen = f.md5 + QStringLiteral(".epub");
    f.finalPath = dir.absoluteFilePath(chosen);
    f.partPath  = f.finalPath + QStringLiteral(".part");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// dev smoke
// ─────────────────────────────────────────────────────────────────────────────

void BookDownloader::selfTest(const QString& md5)
{
    qInfo() << "[BookDownloader] selfTest resolving + downloading md5=" << md5;
    connect(this, &BookDownloader::finished, this, [](const QString& m, const QString& path) {
        qInfo() << "[BookDownloader] selfTest OK md5=" << m << "saved=" << path;
    });
    connect(this, &BookDownloader::failed, this, [](const QString& m, const QString& why) {
        qWarning() << "[BookDownloader] selfTest FAILED md5=" << m << "reason=" << why;
    });
    connect(this, &BookDownloader::progress, this, [](const QString& m, double rcv, double tot) {
        qInfo() << "[BookDownloader] selfTest progress md5=" << m << rcv << "/" << tot;
    });
    downloadBook(md5, QString(), QStringLiteral("selftest"), 0);
}

```


### qml/BiblioBook.qml

```
// BiblioBook — the book "dust-jacket" detail page. Owner: A2. OUR OWN design (NOT the manga series
// view): the cover as a physical object · the tagline as the hero · a drop-capped synopsis · an
// "Editions" panel. Opens as a layer over the Biblio world (Main.qml bookLayer). `book` is a full
// Apple object from BiblioApi.fullBook.
//
// The Editions rows are a STUB until the libgen "delivery" layer is ported (TB2 had it; Colosseum
// doesn't yet). Metadata + layout are real; the download list is a preview.

import QtQuick
import QtQuick.Effects
import "BiblioApi.js" as BiblioApi

Item {
    id: detail
    property var book: ({})
    property Item backdrop
    property var editions: []
    property bool edLoading: false
    property string localPath: ""        // a downloaded edition of this book on disk ("" = none yet)

    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal readRequested(string path, var book)   // a downloaded edition is on disk, ready for the reader

    Theme { id: theme }
    MouseArea { anchors.fill: parent }                 // swallow clicks to the world beneath
    // SOLID page (doctrine: books = page solid, frame OS) — a calm dark reading ground so the busy
    // world page never bleeds through and the long-form text stays legible.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0c0f18" }
            GradientStop { position: 1.0; color: "#06070b" }
        }
    }

    // raised illuminated initial: oversize the first letter inline (QML has no CSS float drop-cap)
    function dropCapHtml(s) {
        var t = String(s || "");
        if (t.length === 0) return "";
        var first = t.charAt(0);
        var rest = t.substring(1);
        return '<span style="font-family:' + theme.display + '; font-size:62px; color:#f7f7f5;">'
             + first + '</span>' + rest;
    }

    // ── editions: live LibGen search for this book (recreates TB2's scraper) ──
    onBookChanged: detail.loadEditions()
    function loadEditions() {
        if (!detail.book || !detail.book.title) return
        detail.edLoading = true
        detail.editions = []
        BiblioApi.searchLibgen(detail.book.title, detail.book.author, function(eds) {
            detail.editions = eds
            detail.edLoading = false
            detail.refreshLocal()
        })
    }
    function edMeta(ed) {
        var p = []
        if (ed.year) p.push(ed.year)
        if (ed.language) p.push(ed.language)
        return p.length ? "   ·   " + p.join("   ·   ") : ""
    }

    // ── download-fed reading: a click pulls the file IN-APP (never out to a browser) ──
    // The native `Books` engine resolves LibGen's fresh key + streams the file to
    // <appdata>/books, then the reader opens that local file. Mirrors Tankoban 2.
    function dlName(ed) {
        var base = (detail.book && detail.book.title) ? detail.book.title : "book"
        return base + "." + ((ed && ed.format) ? ed.format : "epub")
    }
    function bestEdition() {
        for (var i = 0; i < detail.editions.length; i++) if (detail.editions[i].best) return detail.editions[i]
        return detail.editions.length ? detail.editions[0] : null
    }
    function startDownload(ed) {
        if (!ed || typeof Books === 'undefined') return
        Books.downloadBook(ed.md5, detail.dlName(ed),
                           (detail.book && detail.book.title) ? detail.book.title : "", 0)
    }
    function refreshLocal() {
        if (typeof Books === 'undefined') { detail.localPath = ""; return }
        var p = ""
        for (var i = 0; i < detail.editions.length; i++) {
            var lp = Books.localBook(detail.editions[i].md5)
            if (lp) { p = lp; break }
        }
        detail.localPath = p
    }
    Connections {
        target: (typeof Books !== 'undefined') ? Books : null
        function onFinished(md5, path) { detail.refreshLocal() }
    }

    // ── top bar ────────────────────────────────────────────────────────────
    Glass {
        id: bar
        backdrop: detail.backdrop
        x: theme.margin; y: 22
        width: detail.width - theme.margin * 2
        height: 64; radius: 16

        Row {
            anchors.left: parent.left; anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 22
            Text {
                text: "‹ Back"; color: backMa.containsMouse ? theme.ink : theme.inkDim
                font.family: theme.ui; font.pixelSize: 14
                anchors.verticalCenter: parent.verticalCenter
                MouseArea {
                    id: backMa; anchors.fill: parent; anchors.margins: -10
                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: detail.backRequested()
                }
            }
            Text {
                text: "Biblio"; color: theme.ink; font.family: theme.display; font.pixelSize: 20
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Repeater {
                model: [ { g: "—", a: "min" }, { g: "⏻", a: "pow" } ]   // fullscreen-only: no maximize
                delegate: Rectangle {
                    required property var modelData
                    width: 30; height: 30; radius: 8
                    color: sysMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                    Text { anchors.centerIn: parent; text: modelData.g; color: theme.inkDimmer; font.pixelSize: 14 }
                    MouseArea {
                        id: sysMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.a === "min") detail.minimizeRequested()
                            else if (modelData.a === "pow") detail.closeRequested()
                        }
                    }
                }
            }
        }
    }

    // ── scrollable content ─────────────────────────────────────────────────
    Flickable {
        id: page
        anchors.left: parent.left; anchors.right: parent.right
        y: 108; height: detail.height - 108
        contentWidth: width
        contentHeight: body.implicitHeight + 70
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds

        Item {
            id: body
            x: theme.margin
            width: detail.width - theme.margin * 2
            implicitHeight: Math.max(coverCol.implicitHeight, textCol.implicitHeight) + 36

            // ── cover column ──
            Column {
                id: coverCol
                width: 268
                topPadding: 16
                spacing: 28

                // the book as a physical object: soft shadow + cover + spine + page edge
                Item {
                    width: 268; height: 402

                    Rectangle {                       // page edge (right)
                        anchors.right: parent.right; anchors.rightMargin: -5
                        y: 5; width: 7; height: parent.height - 10; radius: 2
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: "#d3cdbe" }
                            GradientStop { position: 1; color: "#a8a294" }
                        }
                    }
                    Image {
                        id: coverImg
                        anchors.fill: parent
                        source: (detail.book && detail.book.cover) ? detail.book.cover : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true; cache: true
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            shadowEnabled: true
                            shadowColor: Qt.rgba(0, 0, 0, 0.7)
                            shadowBlur: 1.0
                            shadowVerticalOffset: 26
                            shadowHorizontalOffset: 0
                            autoPaddingEnabled: true
                        }
                    }
                    Rectangle {                       // base tint while the cover loads
                        anchors.fill: coverImg; z: -1; radius: 3
                        color: (detail.book && detail.book.c1) ? detail.book.c1 : "#14131a"
                    }
                    Rectangle {                       // spine (left)
                        anchors.left: parent.left; width: 11; height: parent.height; radius: 3
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.5) }
                            GradientStop { position: 0.6; color: Qt.rgba(0, 0, 0, 0.05) }
                            GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0.08) }
                        }
                    }
                }

                Column {                              // actions
                    width: 268; spacing: 12
                    Rectangle {
                        id: primaryCta            // one CTA, TB2-style: Get the book → Read (never a browser)
                        width: parent.width; height: 50; radius: 13; color: theme.gold
                        property bool ready: detail.localPath !== ""
                        Text {
                            anchors.centerIn: parent
                            text: primaryCta.ready ? "Read"
                                  : (detail.editions.length ? "Get the book" : "Find the book")
                            color: "#241a05"
                            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                        }
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (primaryCta.ready) detail.readRequested(detail.localPath, detail.book)
                                else detail.startDownload(detail.bestEdition())
                            }
                        }
                    }
                    Rectangle {
                        width: parent.width; height: 50; radius: 13
                        color: libMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                        border.width: 1; border.color: theme.edge
                        Text {
                            anchors.centerIn: parent; text: "+ Library"; color: theme.ink
                            font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                        }
                        MouseArea { id: libMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor }
                    }
                }
            }

            // ── text column ──
            Column {
                id: textCol
                anchors.left: coverCol.right; anchors.leftMargin: 64
                anchors.right: parent.right
                topPadding: 18
                spacing: 0

                Text {                                // eyebrow
                    text: (detail.book && detail.book.genreLine ? detail.book.genreLine : "").toUpperCase()
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 1.8
                }
                Item { width: 1; height: 14 }
                Text {                                // title
                    text: detail.book && detail.book.title ? detail.book.title : ""
                    color: theme.ink; font.family: theme.display; font.pixelSize: 54
                    width: parent.width; wrapMode: Text.WordWrap; lineHeight: 1.02
                }
                Item { width: 1; height: 20 }
                Text {                                // tagline — the hero
                    visible: text.length > 0
                    text: detail.book && detail.book.tagline ? "“" + detail.book.tagline + "”" : ""
                    color: theme.ink; opacity: 0.92
                    font.family: theme.display; font.italic: true; font.pixelSize: 28
                    width: parent.width; wrapMode: Text.WordWrap; lineHeight: 1.3
                }
                Item { width: 1; height: 30 }
                Item {                                // hairline rule with a gold tick
                    width: parent.width; height: 3
                    Rectangle {
                        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                        width: parent.width; height: 1
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: theme.edge }
                            GradientStop { position: 0.7; color: "transparent" }
                        }
                    }
                    Rectangle { anchors.left: parent.left; anchors.top: parent.top; width: 34; height: 3; radius: 2; color: theme.gold }
                }
                Item { width: 1; height: 26 }
                Text {                                // synopsis with a raised initial
                    width: Math.min(parent.width, 640)
                    textFormat: Text.RichText
                    text: detail.dropCapHtml(detail.book ? detail.book.synopsis : "")
                    color: theme.inkDim; font.family: theme.display; font.pixelSize: 17
                    wrapMode: Text.WordWrap; lineHeight: 1.7
                }

                Item { width: 1; height: 40 }
                // ── Editions — live from LibGen (recreates TB2's scraper); click opens the download ──
                Text {
                    text: "EDITIONS  ·  LIBGEN" + (detail.edLoading ? "  ·  SEARCHING…"
                          : (detail.editions.length > 0 ? "  ·  " + detail.editions.length : "  ·  NONE"))
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    font.weight: Font.DemiBold; font.letterSpacing: 1.6
                }
                Item { width: 1; height: 12 }
                Glass {
                    backdrop: detail.backdrop
                    width: Math.min(parent.width, 640); radius: 14
                    height: edCol.implicitHeight
                    Column {
                        id: edCol
                        width: parent.width

                        Item {                              // loading / empty state
                            visible: detail.edLoading || detail.editions.length === 0
                            width: parent.width; height: 52
                            Text {
                                anchors.left: parent.left; anchors.leftMargin: 18
                                anchors.verticalCenter: parent.verticalCenter
                                text: detail.edLoading ? "Searching LibGen…" : "No editions found on LibGen"
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                            }
                        }

                        Repeater {
                            model: detail.editions
                            delegate: Item {
                                id: edRow
                                required property var modelData
                                required property int index
                                width: parent.width; height: 52
                                // download-fed state for THIS edition, reactive via the native Books signals
                                property string dlState: (typeof Books !== 'undefined' && Books.isDownloaded(modelData.md5)) ? "done" : "idle"
                                property real dlPct: 0
                                Connections {
                                    target: (typeof Books !== 'undefined') ? Books : null
                                    function onResolving(md5) { if (md5 === edRow.modelData.md5) edRow.dlState = "resolving" }
                                    function onProgress(md5, rcv, tot) { if (md5 === edRow.modelData.md5) { edRow.dlState = "downloading"; edRow.dlPct = tot > 0 ? rcv / tot : 0 } }
                                    function onFinished(md5, path) { if (md5 === edRow.modelData.md5) { edRow.dlState = "done"; edRow.dlPct = 1 } }
                                    function onFailed(md5, why) { if (md5 === edRow.modelData.md5) edRow.dlState = "failed" }
                                }
                                Rectangle { anchors.fill: parent; color: edMa.containsMouse ? Qt.rgba(1,1,1,0.06)
                                    : (modelData.best ? Qt.rgba(0.94,0.77,0.29,0.06) : "transparent") }
                                Rectangle { visible: index > 0; anchors.top: parent.top; width: parent.width; height: 1; color: Qt.rgba(1,1,1,0.06) }
                                Row {
                                    anchors.left: parent.left; anchors.leftMargin: 18
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 16
                                    Rectangle {
                                        width: 54; height: 24; radius: 7; color: "transparent"
                                        border.width: 1
                                        border.color: modelData.best ? Qt.rgba(0.94,0.77,0.29,0.5) : theme.edge
                                        anchors.verticalCenter: parent.verticalCenter
                                        Text { anchors.centerIn: parent; text: (modelData.format || "?").toUpperCase()
                                            color: modelData.best ? theme.gold : theme.inkDim
                                            font.family: theme.ui; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 0.8 }
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "<b>" + (modelData.size || "") + "</b>" + detail.edMeta(modelData)
                                        textFormat: Text.RichText
                                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                                    }
                                }
                                Text {
                                    anchors.right: parent.right; anchors.rightMargin: 18
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: edRow.dlState === "done" ? "✓"
                                        : edRow.dlState === "downloading" ? (Math.round(edRow.dlPct * 100) + "%")
                                        : edRow.dlState === "resolving" ? "…"
                                        : edRow.dlState === "failed" ? "retry" : "↓"
                                    color: edRow.dlState === "done" ? theme.gold : (edMa.containsMouse ? theme.gold : theme.inkDimmer)
                                    font.family: theme.ui
                                    font.pixelSize: (edRow.dlState === "downloading" || edRow.dlState === "failed") ? 12 : 16
                                }
                                MouseArea { id: edMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (edRow.dlState === "done") detail.readRequested(Books.localBook(edRow.modelData.md5), detail.book)
                                        else if (edRow.dlState !== "downloading" && edRow.dlState !== "resolving") detail.startDownload(edRow.modelData)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

```


### qml/BiblioWorld.qml

```
// BiblioWorld - the Colosseum world page for books. Owner: A2.
// Same spine as Tankoban/Theatre: Featured carousel, Continue, Top-10, genres.
//
// Discovery = Apple Books charts via BiblioApi (live, daily-fresh). Catalog.biblio* is the static
// fallback so the page paints instantly and never sits empty if the live call is slow. Delivery
// (search + download) stays libgen from TB2 - a separate layer, like Cinemeta vs the Theatre addon.

import QtQuick
import "Catalog.js" as Catalog
import "BiblioApi.js" as BiblioApi

WorldPage {
    id: biblio
    medium: "Biblio"

    property var featuredRows: Catalog.biblioFeatured
    property var topRows: Catalog.biblioTop
    property var genreRows: Catalog.biblioGenres
    signal biblioGenreRequested(string genreName)

    // Live override: swap in Apple's fresh chart once it lands; keep the static fallback on failure.
    Component.onCompleted: BiblioApi.loadBiblio(function(rows) {
        if (rows.featured && rows.featured.length > 0)
            biblio.featuredRows = rows.featured
        if (rows.top && rows.top.length > 0)
            biblio.topRows = rows.top
    })

    // tap a book → fetch its full detail by title, then open the dust-jacket page
    function openByTitle(title) {
        if (!title) return
        BiblioApi.lookupBook(title, function(b) { if (b) biblio.bookRequested(b) })
    }

    FeaturedCarousel {
        kicker: "Featured in Biblio"
        primaryLabel: "Read"
        secondaryLabel: "Details"
        slides: biblio.featuredRows
        onPrimaryClicked: (i) => biblio.openByTitle(biblio.featuredRows[i] ? biblio.featuredRows[i].title : "")
        onSecondaryClicked: (i) => biblio.openByTitle(biblio.featuredRows[i] ? biblio.featuredRows[i].title : "")
    }

    ContinueRow {
        title: "Continue"
        items: (Progress.revision, Progress.recent("book", 12))
        onResumeRequested: (item) => biblio.continueResumeRequested(item)
        onDetailRequested: (item) => biblio.continueDetailRequested(item)
    }

    TrendingTop10 {
        title: "Top 10 in Biblio"
        items: biblio.topRows
        onItemClicked: (i) => biblio.openByTitle(biblio.topRows[i] ? biblio.topRows[i].caption : "")
    }

    GenreMosaic {
        title: "Browse Biblio"
        genres: biblio.genreRows
        onGenreClicked: (i) => biblio.biblioGenreRequested(biblio.genreRows[i].name)
    }
}

```


### qml/MangaSeries.qml

```
// MangaSeries — the manga detail page (Tankoban mode). Colosseum series-view design (mock:
// agents/colosseum-series-mock.html, Hemanth-approved 2026-06-27): a series IS its volumes, so the
// VOLUME SHELF of real tankōbon covers is the hero AND the navigation — pick a cover and the glass
// chapter table below re-headers to it. Floats over the wallpaper; metadata is inline (no glass pills);
// gold stays a sparing accent. Data is LIVE from the native engine via the `Manga` bridge:
//   title → WeebCentral search → (chapters + detail)
//         → AniList art()      → banner / cover / synopsis / genres / year / score
//         → MangaFire volumes()→ clean per-volume covers + chapter ranges (normalized in MangaVolumes.js)
// Opened from a Top-10 manga tile.

import QtQuick
import "MangaVolumes.js" as Vol

Item {
    id: page
    property Item backdrop
    property string seriesTitle: ""
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()

    // --- resolved state ---
    property string seriesId: ""
    property string seriesUrl: ""
    property string banner: ""
    property string cover: ""
    property string author: ""
    property string status: ""
    property int    year: 0
    property string synopsis: ""
    property var genres: []
    property int score: 0
    property var chaptersModel: []
    property bool loading: true
    property string errorMsg: ""

    // --- seamless reveal gate ---
    // The page fires WeebCentral (chapters), AniList (art) and MangaFire (volumes) in parallel,
    // each at a different speed. We must NEVER reveal the page until ALL three are in — otherwise
    // the user sees the flat chapter list / low-q art first and watches it reflow. _maybeReveal()
    // drops `loading` only when everything is ready, so the page appears once, already finished.
    property bool chaptersReady: false
    property bool artReady: false
    property bool volumesReady: false
    function _maybeReveal() {
        if (chaptersReady && artReady && volumesReady) { loading = false; revealGuard.stop() }
    }

    // --- volumes (MangaFire via MangaVolumes.js) ---
    property var volumes: []                                  // [{number,cover,startNum,endNum,chapterStart,chapterEnd}]
    property var volGroups: Vol.group(chaptersModel, volumes) // { options:[{key,label}], byKey:{} }
    property string activeVol: ""                             // user's pick (volume number as string)
    property string shownVol: (activeVol.length && volGroups.byKey && volGroups.byKey[activeVol] !== undefined)
                              ? activeVol
                              : (volGroups.options.length ? volGroups.options[0].key : "")
    // the chapters actually shown: the active volume's, or the flat list when there's no volume data
    property var visibleChapters: loading ? []
        : (volGroups.options.length ? (volGroups.byKey[shownVol] || []) : chaptersModel)

    function volRange(volNum) {
        for (var i = 0; i < volumes.length; i++)
            if (String(volumes[i].number) === volNum)
                return volumes[i].chapterStart + "–" + volumes[i].chapterEnd
        return ""
    }

    Theme { id: theme }

    onSeriesTitleChanged: resolve()
    Component.onCompleted: if (seriesTitle.length) resolve()

    function resolve() {
        loading = true; errorMsg = ""
        seriesId = ""; banner = ""; cover = ""; author = ""; status = ""; year = 0
        synopsis = ""; genres = []; score = 0; chaptersModel = []
        volumes = []; activeVol = ""
        chaptersReady = false; artReady = false; volumesReady = false
        if (seriesTitle.length) {
            revealGuard.restart()        // never hang on a dead source — reveal what we have after N s
            Manga.search(seriesTitle)    // → chapters + WeebCentral detail
            Manga.art(seriesTitle)       // → AniList banner / cover / synopsis / genres / year
            Manga.volumes(seriesTitle)   // → MangaFire volume structure (covers + chapter ranges)
        }
    }

    // Safety net: if a source never answers, reveal after this timeout rather than spin forever.
    Timer { id: revealGuard; interval: 12000; repeat: false; onTriggered: page.loading = false }

    function fmtDate(ms) {
        var n = Number(ms)
        if (!n || n <= 0) return ""
        return new Date(n).toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }

    Connections {
        target: Manga
        function onSearchResults(results) {
            if (results.length === 0) {
                page.errorMsg = "“" + page.seriesTitle + "” wasn’t found on WeebCentral."
                page.loading = false
                return
            }
            var r = results[0]
            page.seriesId = r.id; page.seriesUrl = r.url
            // NOTE: deliberately do NOT take WeebCentral's low-res cover for the banner — that was
            // the source of the "low-q art that changes after a while" swap. The banner comes only
            // from AniList (hi-res), set in onArtResult.
            page.author = r.author; page.status = r.status
            Manga.chapters(r.id)
            Manga.detail(r.id, r.url, r.title, r.cover)
        }
        function onChaptersResults(chs) { page.chaptersModel = chs; page.chaptersReady = true; page._maybeReveal() }
        function onDetailResult(d) {
            // AniList is the source for synopsis + genres (onArtResult). WeebCentral detail only
            // contributes status + author — NOT its plainer description (AniList's reads better).
            if (d.status && d.status.length) page.status = d.status
            if (d.author && d.author.length) page.author = d.author
        }
        function onArtResult(a) {
            if (a.banner && a.banner.length) page.banner = a.banner
            if (a.cover && a.cover.length) page.cover = a.cover
            if (a.description && a.description.length) page.synopsis = a.description
            if (a.genres && a.genres.length) page.genres = a.genres
            if (a.score) page.score = a.score
            if (a.year) page.year = a.year
            page.artReady = true; page._maybeReveal()
        }
        function onVolumesResult(d) { page.volumes = Vol.fromMangaFire(d.volumes || []); page.volumesReady = true; page._maybeReveal() }
        function onEngineError(msg) { if (page.loading) { page.errorMsg = msg; page.loading = false; revealGuard.stop() } }
    }

    // ===================== visual tree =====================
    MouseArea { anchors.fill: parent }                          // absorb clicks from the world page below

    // Base: a live mirror of the wallpaper so the series view FLOATS over the backdrop (the doctrine's
    // "fancy OS-widget table over the backdrop") while still hiding the world page it sits on top of.
    Rectangle { anchors.fill: parent; color: "#07080c" }        // fallback if backdrop is ever null
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: page.backdrop
        live: true; hideSource: false
        visible: page.backdrop !== null
    }
    // adaptive scrim — keeps text + chrome legible over any wallpaper, darker toward the chapter list
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.03, 0.04, 0.06, 0.42) }
            GradientStop { position: 0.42; color: Qt.rgba(0.03, 0.035, 0.05, 0.72) }
            GradientStop { position: 1.0; color: Qt.rgba(0.02, 0.025, 0.04, 0.9) }
        }
    }

    // ---- top scrim so the back/window controls read against ANY background (bright banner or dark) ----
    ChromeScrim { z: 16 }

    // ---- ‹ Back (pinned, floats over the banner) ----
    Item {
        id: backBtn
        x: theme.margin; y: 28; width: backRow.implicitWidth + 16; height: 34; z: 20
        Row {
            id: backRow; anchors.verticalCenter: parent.verticalCenter; spacing: 6
            Text { text: "‹"; color: backMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.display; font.pixelSize: 26; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Back"; color: backMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.ui; font.pixelSize: 15; anchors.verticalCenter: parent.verticalCenter
                Behavior on color { ColorAnimation { duration: 120 } } }
        }
        MouseArea { id: backMa; anchors.fill: parent; anchors.margins: -8; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor; onClicked: page.backRequested() }
    }

    // ---- window controls (minimize / power) — the SAME icons as the home/world top bar ----
    Row {
        z: 30
        anchors.right: parent.right; anchors.rightMargin: theme.margin; y: 34
        spacing: 20
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/minimize.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: minMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: minMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: page.minimizeRequested() }
        }
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/power.svg"
                sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit
                opacity: clMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: clMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: page.closeRequested() }
        }
    }

    // ---- the page: one vertical scroll; banner → synopsis → volume shelf → glass chapter table ----
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: pageCol.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        // The whole page stays invisible until fully assembled, then fades in as one finished piece.
        opacity: page.loading ? 0.0 : 1.0
        Behavior on opacity { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

        Column {
            id: pageCol
            width: flick.width
            spacing: 0

            // ── BANNER HERO (full-bleed art; content inset to the margin) ──
            Item {
                width: parent.width
                height: 360

                Image {
                    id: bannerImg
                    anchors.fill: parent
                    source: page.banner.length ? page.banner : page.cover
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true; cache: true
                    // soft fade in when the pixels arrive — never a hard pop, even on first load
                    opacity: status === Image.Ready ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                }
                // wash the banner down into the page so it reads as one surface (IP color stays up top)
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(0.03, 0.035, 0.055, 0.15) }
                        GradientStop { position: 0.55; color: Qt.rgba(0.03, 0.035, 0.05, 0.45) }
                        GradientStop { position: 1.0; color: Qt.rgba(0.02, 0.025, 0.04, 0.92) }
                    }
                }

                Column {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin; anchors.bottomMargin: 30
                    spacing: 12

                    Text {
                        text: "Manga · Tankoban"
                        color: theme.gold; font.family: theme.ui; font.pixelSize: 11
                        font.letterSpacing: 3; font.capitalization: Font.AllUppercase
                    }
                    Text {
                        width: parent.width
                        text: page.seriesTitle
                        color: theme.ink; font.family: theme.display; font.pixelSize: 64
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                        style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.35)
                    }
                    // INLINE metadata — author (bright) · status · year · ★score · genres. No glass pills.
                    Row {
                        spacing: 11
                        Text { visible: page.author.length; text: page.author
                            color: theme.ink; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.author.length && (page.status.length || page.year)
                            text: "·"; color: theme.inkDimmer; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.status.length; text: page.status
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.status.length && page.year
                            text: "·"; color: theme.inkDimmer; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.year > 0; text: page.year
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.score > 0
                            text: "·"; color: theme.inkDimmer; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.score > 0; text: "★ " + page.score
                            color: theme.gold; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.genres.length > 0
                            text: "·"; color: theme.inkDimmer; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: page.genres.length > 0
                            text: page.genres.slice(0, 3).join(" · ")
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                    }
                    // Primary CTA — Read. (Per-volume download moved down to the chapter-table header.)
                    Row {
                        spacing: 12
                        topPadding: 8
                        Rectangle {
                            width: readRow.implicitWidth + 40; height: 42; radius: 11; color: theme.gold
                            Row {
                                id: readRow; anchors.centerIn: parent; spacing: 9
                                Text { text: "▶"; color: "#1a1306"; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: "Read"; color: "#1a1306"; font.family: theme.ui; font.pixelSize: 14
                                    font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                            }
                            MouseArea { anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onEntered: parent.opacity = 0.92; onExited: parent.opacity = 1.0 }
                        }
                    }
                }
            }

            // ── synopsis (inset) ──
            Text {
                visible: page.synopsis.length > 0
                x: theme.margin
                width: Math.min(880, parent.width - 2 * theme.margin)
                text: page.synopsis
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 15
                lineHeight: 1.5; wrapMode: Text.WordWrap
                topPadding: 22; bottomPadding: 6
            }

            // ── VOLUMES — the signature shelf ──
            Item {
                width: parent.width; height: volumesSec.height
                visible: page.volumes.length > 0
                Column {
                    id: volumesSec
                    width: parent.width
                    spacing: 0
                    // section label + bright count (inline, never a pill)
                    Row {
                        x: theme.margin; spacing: 11; topPadding: 30; bottomPadding: 14
                        Text { text: "VOLUMES"; color: theme.inkDimmer; font.family: theme.display
                            font.pixelSize: 13; font.letterSpacing: 3 }
                        Text { text: page.volumes.length; color: theme.ink; font.family: theme.display
                            font.pixelSize: 15; font.weight: Font.Bold }
                    }
                    // the shelf: real tankōbon covers, scrollable, sitting on a ledge
                    Item {
                        width: parent.width; height: 214

                        // the ledge the books sit on (fixed; covers scroll over it)
                        Rectangle {
                            anchors.left: parent.left; anchors.right: parent.right
                            anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
                            anchors.bottom: parent.bottom; anchors.bottomMargin: 20
                            height: 1; opacity: 0.18
                            gradient: Gradient { orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: "transparent" }
                                GradientStop { position: 0.06; color: theme.ink }
                                GradientStop { position: 0.94; color: theme.ink }
                                GradientStop { position: 1.0; color: "transparent" } }
                        }

                        ListView {
                            id: shelf
                            anchors.fill: parent
                            orientation: ListView.Horizontal
                            leftMargin: theme.margin; rightMargin: theme.margin
                            spacing: 18
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: page.volumes

                            delegate: Item {
                                id: vtile
                                required property var modelData
                                width: 116; height: 206
                                property bool on: String(modelData.number) === page.shownVol
                                Column {
                                    width: parent.width; spacing: 9
                                    Item {
                                        width: 116; height: 166
                                        y: vtile.on ? -12 : (vtMa.containsMouse ? -6 : 0)
                                        Behavior on y { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                                        // frame + fallback (shows the number when a cover is missing)
                                        Rectangle {
                                            anchors.fill: parent
                                            color: "#1a1c24"
                                            border.width: vtile.on ? 2 : 1
                                            border.color: vtile.on ? Qt.rgba(0.94,0.77,0.29,0.9) : Qt.rgba(1,1,1,0.12)
                                            Text {
                                                anchors.centerIn: parent
                                                visible: cover.status !== Image.Ready
                                                text: vtile.modelData.number
                                                color: Qt.rgba(1,1,1,0.5); font.family: theme.display
                                                font.pixelSize: 40; font.weight: Font.Bold
                                            }
                                        }
                                        Image {
                                            id: cover
                                            anchors.fill: parent
                                            anchors.margins: vtile.on ? 2 : 1
                                            source: vtile.modelData.cover ? vtile.modelData.cover : ""
                                            visible: status === Image.Ready
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true; cache: true
                                        }
                                    }
                                    Column {
                                        width: parent.width; spacing: 5
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: "Vol " + vtile.modelData.number
                                            color: vtile.on ? theme.gold : theme.inkDim
                                            font.family: theme.ui; font.pixelSize: 12
                                            font.weight: vtile.on ? Font.DemiBold : Font.Normal
                                        }
                                        Rectangle {
                                            visible: vtile.on
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: 22; height: 2; radius: 2; color: theme.gold
                                        }
                                    }
                                }
                                MouseArea {
                                    id: vtMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: page.activeVol = String(vtile.modelData.number)
                                }
                            }
                        }

                        // ── sideways nav: chevrons appear when there's more shelf to scroll ──
                        NumberAnimation { id: shelfAnim; target: shelf; property: "contentX"
                            duration: 320; easing.type: Easing.OutCubic }
                        Rectangle {
                            id: navLeft
                            visible: shelf.contentX > 2
                            anchors.left: parent.left; anchors.leftMargin: 12
                            y: 60; width: 44; height: 44; radius: 22
                            color: navLeftMa.containsMouse ? Qt.rgba(0,0,0,0.66) : Qt.rgba(0,0,0,0.44)
                            border.width: 1; border.color: theme.edge
                            Text { anchors.centerIn: parent; text: "‹"; color: theme.ink
                                font.family: theme.display; font.pixelSize: 28 }
                            MouseArea {
                                id: navLeftMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: { shelfAnim.stop(); shelfAnim.to = Math.max(shelf.contentX - shelf.width * 0.8, 0); shelfAnim.start() }
                            }
                        }
                        Rectangle {
                            id: navRight
                            visible: shelf.contentX < shelf.contentWidth - shelf.width - 2
                            anchors.right: parent.right; anchors.rightMargin: 12
                            y: 60; width: 44; height: 44; radius: 22
                            color: navRightMa.containsMouse ? Qt.rgba(0,0,0,0.66) : Qt.rgba(0,0,0,0.44)
                            border.width: 1; border.color: theme.edge
                            Text { anchors.centerIn: parent; text: "›"; color: theme.ink
                                font.family: theme.display; font.pixelSize: 28 }
                            MouseArea {
                                id: navRightMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: { shelfAnim.stop(); shelfAnim.to = Math.min(shelf.contentX + shelf.width * 0.8, shelf.contentWidth - shelf.width); shelfAnim.start() }
                            }
                        }
                    }
                }
            }

            // ── CHAPTER TABLE — the floating glass OS-widget; header = selected volume ──
            Item {
                width: parent.width
                height: chTable.height + 24
                visible: page.visibleChapters.length > 0

                Glass {
                    id: chTable
                    x: theme.margin
                    width: parent.width - 2 * theme.margin
                    height: tableInner.height
                    radius: 18
                    backdrop: page.backdrop
                    track: flick.contentY               // recompute blur as the page scrolls

                    Column {
                        id: tableInner
                        width: parent.width
                        // header
                        Item {
                            width: parent.width; height: 58
                            Row {
                                anchors.left: parent.left; anchors.leftMargin: 24
                                anchors.verticalCenter: parent.verticalCenter; spacing: 14
                                Text { text: "Vol. " + page.shownVol; color: theme.ink
                                    font.family: theme.display; font.pixelSize: 19; font.weight: Font.DemiBold
                                    anchors.verticalCenter: parent.verticalCenter }
                                Text { text: page.visibleChapters.length + " chapters"; color: theme.inkDim
                                    font.family: theme.ui; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                                // per-volume download — lives with the volume it acts on (DL wired in a later layer)
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: dlVolRow.implicitWidth + 26; height: 30; radius: 8
                                    color: dlVolMa.containsMouse ? theme.glassHi : theme.glassTint
                                    border.width: 1
                                    border.color: dlVolMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.55) : theme.edge
                                    Row {
                                        id: dlVolRow; anchors.centerIn: parent; spacing: 7
                                        Text { text: "↓"; color: theme.ink; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                                        Text { text: "Download volume"; color: theme.inkDim; font.family: theme.ui
                                            font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                                    }
                                    MouseArea { id: dlVolMa; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (typeof Downloads === "undefined") return
                                            var chs = page.visibleChapters
                                            for (var i = 0; i < chs.length; i++) {
                                                var id = String(chs[i].id || "")
                                                if (!id.length) continue
                                                var lbl = (chs[i].name && String(chs[i].name).length)
                                                          ? chs[i].name : ("Chapter " + (chs[i].number || ""))
                                                Downloads.downloadChapter(id, page.seriesId, page.seriesTitle, lbl)
                                            }
                                        } }
                                }
                            }
                            Text {
                                anchors.right: parent.right; anchors.rightMargin: 24
                                anchors.verticalCenter: parent.verticalCenter
                                text: { var r = page.volRange(page.shownVol); return r.length ? "Ch " + r : "" }
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                            }
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.edge }
                        }
                        // chapter rows (selected volume → bounded count → a Repeater is fine)
                        Repeater {
                            model: page.visibleChapters
                            delegate: Item {
                                id: row
                                required property var modelData
                                width: tableInner.width; height: 92

                                // per-row download state, kept live via the Downloads signals
                                property string chId: String(row.modelData.id || "")
                                property string dlState: "none"   // none | queued | downloading | done | error
                                property int dlDone: 0
                                property int dlTotal: 0
                                readonly property bool inFlight: dlState === "downloading" || dlState === "queued"
                                property string liveThumb: ""   // first-page url for an UNdownloaded chapter (scraped)
                                // chapter thumbnail = its FIRST page: downloaded -> local file (instant),
                                // else the scraped first-page url resolved via Downloads.fetchThumb.
                                readonly property string thumbUrl: dlState === "done" ? row.firstLocalUrl() : row.liveThumb
                                function firstLocalUrl() {
                                    if (typeof Downloads === "undefined") return ""
                                    var lp = Downloads.localPages(row.chId)
                                    return (lp && lp.length) ? lp[0].url : ""
                                }
                                function chLabel() {
                                    return (row.modelData.name && String(row.modelData.name).length)
                                        ? row.modelData.name : ("Chapter " + (row.modelData.number || ""))
                                }
                                function statusLine() {
                                    if (dlState === "done") return "● Downloaded"
                                    if (dlState === "queued") return "Queued…"
                                    if (dlState === "downloading")
                                        return dlTotal > 0 ? ("Downloading " + Math.round(dlDone / dlTotal * 100) + "%") : "Downloading…"
                                    if (dlState === "error") return "⚠ Failed — tap to retry"
                                    return ""
                                }
                                function openReader() {
                                    page.openChapterId = row.chId
                                    page.openChapterLabel = row.chLabel()
                                }
                                function startDownload() {
                                    if (typeof Downloads === "undefined" || !row.chId.length) return
                                    row.dlState = "queued"
                                    Downloads.downloadChapter(row.chId, page.seriesId, page.seriesTitle, row.chLabel())
                                }
                                // download-fed: tap reads a downloaded chapter, else downloads it (the reader only opens what's on disk)
                                function primary() {
                                    if (row.dlState === "done") row.openReader()
                                    else if (!row.inFlight) row.startDownload()
                                }
                                function refreshDl() {
                                    if (typeof Downloads === "undefined") return
                                    var st = Downloads.statusOf(row.chId)
                                    row.dlState = st.state; row.dlDone = st.done; row.dlTotal = st.total
                                }
                                function requestThumb() {
                                    if (typeof Downloads !== "undefined") Downloads.fetchThumb(page.seriesId, row.chId)
                                }
                                Component.onCompleted: { refreshDl(); requestThumb() }
                                Connections {
                                    target: typeof Downloads !== "undefined" ? Downloads : null
                                    function onProgress(cid, done, total) {
                                        if (cid !== row.chId) return
                                        row.dlState = "downloading"; row.dlDone = done; row.dlTotal = total
                                    }
                                    function onFinished(cid) { if (cid === row.chId) row.dlState = "done" }
                                    function onFailed(cid, reason) { if (cid === row.chId) row.dlState = "error" }
                                    function onThumbReady(cid, url) { if (cid === row.chId && url.length) row.liveThumb = url }
                                    function onRemoved(cid) {
                                        if (cid !== row.chId) return
                                        row.dlState = "none"; row.liveThumb = ""; row.requestThumb()
                                    }
                                }

                                Rectangle { anchors.fill: parent; color: rowMa.containsMouse ? Qt.rgba(1,1,1,0.05) : "transparent" }

                                // thumbnail (portrait) — first page once downloaded, numbered placeholder otherwise
                                Item {
                                    id: thumb
                                    anchors.left: parent.left; anchors.leftMargin: 22
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 58; height: 80
                                    Rectangle {
                                        anchors.fill: parent; radius: 6; color: "#15171f"; border.width: 1
                                        border.color: row.dlState === "done" ? Qt.rgba(0.94,0.77,0.29,0.5) : theme.edge
                                        Text { anchors.centerIn: parent; visible: thumbImg.status !== Image.Ready
                                            text: row.modelData.number || "?"; color: theme.inkDimmer
                                            font.family: theme.display; font.pixelSize: 22 }
                                    }
                                    Image { id: thumbImg; anchors.fill: parent; anchors.margins: 1
                                        source: row.thumbUrl; visible: status === Image.Ready
                                        fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true
                                        sourceSize.width: 170 }
                                }

                                // title + status subtitle
                                Column {
                                    anchors.left: thumb.right; anchors.leftMargin: 16
                                    anchors.right: trailing.left; anchors.rightMargin: 14
                                    anchors.verticalCenter: parent.verticalCenter; spacing: 4
                                    Text { width: parent.width; text: row.chLabel()
                                        color: rowMa.containsMouse ? theme.gold : theme.ink
                                        font.family: theme.ui; font.pixelSize: 17; elide: Text.ElideRight }
                                    Text { width: parent.width; text: row.statusLine(); visible: text.length > 0
                                        color: row.dlState === "done" ? theme.gold
                                             : (row.dlState === "error" ? "#e6a3a3" : theme.inkDimmer)
                                        font.family: theme.ui; font.pixelSize: 13; elide: Text.ElideRight }
                                }

                                // trailing control: ✓→✕ delete (done) · ✕ cancel (in-flight) · ↓/↻ download/retry
                                Item {
                                    id: trailing
                                    anchors.right: parent.right; anchors.rightMargin: 22
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 36; height: 36
                                    Rectangle { anchors.fill: parent; radius: 18
                                        color: trMa.containsMouse ? theme.glassHi : "transparent" }
                                    Text {
                                        anchors.centerIn: parent
                                        text: row.dlState === "done" ? (trMa.containsMouse ? "✕" : "✓")
                                            : row.inFlight ? "✕"
                                            : row.dlState === "error" ? "↻" : "↓"
                                        color: (row.dlState === "done" && trMa.containsMouse) ? "#e6a3a3"
                                             : row.dlState === "done" ? theme.gold
                                             : trMa.containsMouse ? theme.gold : theme.inkDim
                                        font.pixelSize: 16
                                        font.weight: (row.dlState === "done" && !trMa.containsMouse) ? Font.Bold : Font.Normal
                                    }
                                    MouseArea {
                                        id: trMa; anchors.fill: parent; hoverEnabled: true; z: 5
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (typeof Downloads === "undefined") return
                                            if (row.dlState === "done") Downloads.deleteChapter(row.chId)
                                            else if (row.inFlight) Downloads.cancelDownload(row.chId)
                                            else row.startDownload()
                                        }
                                    }
                                }

                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                                    color: Qt.rgba(1,1,1,0.05); visible: row.y + row.height < tableInner.height }
                                MouseArea { id: rowMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: row.primary() }
                            }
                        }
                    }
                }
            }

            // post-reveal error (inset)
            Text {
                visible: !page.loading && page.errorMsg.length > 0
                x: theme.margin
                text: page.errorMsg
                color: "#e6a3a3"; font.family: theme.ui; font.pixelSize: 13
                topPadding: 18
            }

            Item { width: 1; height: 70 }   // bottom breathing room
        }
    }

    // ---- clean loading state ----
    // Shown while the page assembles; it fades out as the finished page fades in (see Flickable opacity),
    // so the user sees one calm transition — never the flat list or low-q art being built in front of them.
    Column {
        id: loadingState
        visible: page.loading
        opacity: page.loading ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        anchors.centerIn: parent
        width: parent.width * 0.7
        spacing: 14
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: page.seriesTitle
            color: theme.ink; font.family: theme.display; font.pixelSize: 34
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
        }
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: page.errorMsg.length ? page.errorMsg : "Loading…"
            color: page.errorMsg.length ? "#e6a3a3" : theme.inkDim
            font.family: theme.ui; font.pixelSize: 14
        }
    }

    // ---- reader overlay: opened from a chapter row (the recreated Tankoban reader) ----
    // Direct child (NOT a Loader+inline-Component): inside a nested Component the outer
    // `page` id does not resolve, so every page.* binding was undefined and onBackRequested
    // silently threw — the reader could never close. As a direct child, `page` resolves
    // like everywhere else. Idle cost is nil: with no chapterId it fetches nothing and
    // visible:false removes it from input.
    property string openChapterId: ""
    property string openChapterLabel: ""
    MangaReader {
        id: readerLayer
        anchors.fill: parent; z: 60
        visible: page.openChapterId.length > 0
        backdrop: page.backdrop
        seriesTitle: page.seriesTitle
        seriesId: page.seriesId
        seriesCover: page.cover
        chapters: page.chaptersModel
        chapterId: page.openChapterId
        chapterLabel: page.openChapterLabel
        onBackRequested: { page.openChapterId = ""; page.openChapterLabel = "" }
        onMinimizeRequested: page.minimizeRequested()
        onCloseRequested: page.closeRequested()
    }
}

```


### qml/MangaReader.qml

```
// MangaReader — native QML recreation of Tankoban Electron's MangaReader.jsx (recreate, not
// redesign). Driven by ReaderEngine.js (the verbatim layout gem) + the download-fed page source.
// PASS 2: the real reader chrome — persisted prefs (QtCore Settings, app-wide), the three modals
// (chapter grid / page-jump grid / preferences), chapter-crossing prev·next, MangaPlus
// double_page_v2, page-width control, and auto-hide chrome. Reading is DOWNLOAD-FED: pages are
// the local files from Downloads.localPages(); an undownloaded chapter shows "go download it".
import QtQuick
import QtCore
import "ReaderEngine.js" as Engine

Item {
    id: reader
    property Item backdrop
    property string seriesTitle: ""
    property string seriesId: ""
    property string seriesCover: ""           // series cover (from the series view) — for the Continue card
    property var    chapters: []              // ALL chapters (newest-first) — for the modal + crossing
    property string chapterId: ""             // incoming open target (from the series view)
    property string chapterLabel: ""          // incoming fallback label
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()

    // --- preferences (app-wide, persisted; mirrors Electron mangaPrefs) ---
    Settings {
        id: prefs
        category: "mangaReader"
        property string reading_style: "long_strip"      // long_strip|single_page|double_page|double_page_v2
        property string reading_direction: "right_left"  // left_right|right_left (manga default RTL)
        property string image_fit: "width"               // width|height
        property bool   gap: true
        property bool   dark_background: true
        property bool   back_to_top: true
        property bool   sticky_top_nav: false
        property int    portrait_width_pct: 100
    }
    readonly property string style: prefs.reading_style
    readonly property string fit: prefs.image_fit
    readonly property int    portraitWidthPct: prefs.portrait_width_pct
    // MangaPlus (double_page_v2) reads LEFT-TO-RIGHT regardless of the saved direction.
    readonly property bool rtl: style === "double_page_v2" ? false : prefs.reading_direction === "right_left"
    readonly property bool isDouble: style === "double_page" || style === "double_page_v2"
    readonly property bool paged: style === "single_page" || isDouble

    // --- which chapter we're actually reading (the modal / crossing can change it) ---
    property string curChapterId: ""
    property bool   pendingAtLast: false        // open an older chapter at its last page
    onChapterIdChanged: curChapterId = chapterId
    Component.onCompleted: { curChapterId = chapterId; if (curChapterId.length) load() }
    onCurChapterIdChanged: { load(); recordProgress() }
    // grab keyboard focus whenever the reader is shown, so arrows/Esc work (it's a
    // direct child of the series page and won't get active focus on its own).
    onVisibleChanged: if (visible) reader.forceActiveFocus()

    readonly property int curIndex: {
        for (var i = 0; i < chapters.length; i++)
            if (String(chapters[i].id) === curChapterId) return i
        return -1
    }
    readonly property string curLabel: {
        var c = curIndex >= 0 ? chapters[curIndex] : null
        if (c) return (c.name && String(c.name).length) ? c.name : ("Chapter " + (c.number || ""))
        return chapterLabel
    }
    // newest-first: index-1 = newer (forward read), index+1 = older (previous)
    readonly property bool hasNewer: curIndex > 0
    readonly property bool hasOlder: curIndex >= 0 && curIndex < chapters.length - 1

    // --- data ---
    property var  pagesModel: []               // [{index, url, group}] — LOCAL file:/// urls
    property int  page: 1                       // 1-based current page (anchor in double mode)
    property bool loading: true
    property string errorMsg: ""
    property bool downloading: false
    property int  dlDone: 0
    property int  dlTotal: 0
    property var  dims: ({})                    // { index: {w,h} } natural px
    property int  couplingNudge: 0
    property bool atEnd: false                  // "all caught up" end card

    readonly property int  max: pagesModel.length

    // --- continue tracking: note how far into this series we've read, for the Continue row ---
    function recordProgress() {
        if (typeof Progress === "undefined" || !reader.seriesId.length || reader.max <= 0)
            return
        Progress.record({
            "id": reader.seriesId,
            "kind": "manga",
            "caption": reader.seriesTitle,
            "title": reader.seriesTitle,
            "sub": reader.curLabel,
            "cover": reader.seriesCover,
            "c1": "#3a2f55", "c2": "#15111f",
            "progress": Math.min(1, Math.max(0, reader.page / reader.max)),
            "resume": { "chapterId": reader.curChapterId, "page": reader.page }
        })
    }
    onPageChanged: recordProgress()

    // --- modals + HUD popups ---
    property bool showPrefs: false
    property bool showJump: false
    property bool showChapters: false
    property string hudMenu: ""                 // "mode" | "width" | ""

    Theme { id: theme }

    function load() {
        errorMsg = ""; dims = ({}); loading = false; atEnd = false
        pagesModel = (curChapterId.length && typeof Downloads !== "undefined")
                     ? Downloads.localPages(curChapterId) : []
        if (pagesModel.length > 0) {
            downloading = false
            var start = pendingAtLast ? pagesModel.length : 1
            pendingAtLast = false
            page = isDouble ? (Engine.snapTwoPageIndex(start - 1,
                       { n: pagesModel.length, isSpreadAt: function () { return false }, couplingNudge: couplingNudge }) + 1)
                            : start
            return
        }
        page = 1; pendingAtLast = false
        var st = (curChapterId.length && typeof Downloads !== "undefined")
                 ? Downloads.statusOf(curChapterId) : { state: "none", done: 0, total: 0 }
        downloading = (st.state === "downloading" || st.state === "queued")
        dlDone = st.done; dlTotal = st.total
    }

    function startDownload() {
        if (!curChapterId.length || typeof Downloads === "undefined") return
        downloading = true; errorMsg = ""
        Downloads.downloadChapter(curChapterId, seriesId, seriesTitle, curLabel)
    }

    function reportDims(i, w, h) {
        if (!w || !h || dims[i]) return
        var d = dims; d[i] = { w: w, h: h }; dims = d
    }
    function ctx() {
        return { n: reader.max,
                 isSpreadAt: function (i) { var d = reader.dims[i]; return d ? Engine.isSpread(d.w, d.h) : false },
                 couplingNudge: reader.couplingNudge }
    }
    readonly property int anchor: (isDouble && max) ? Engine.snapTwoPageIndex(page - 1, ctx()) : page - 1
    readonly property var pair: (isDouble && max) ? Engine.getTwoPagePair(anchor, ctx()) : null
    readonly property string curUrl: (page >= 1 && page <= max) ? pagesModel[page - 1].url : ""

    Connections {
        target: typeof Downloads !== "undefined" ? Downloads : null
        function onProgress(cid, done, total) {
            if (cid !== reader.curChapterId) return
            reader.downloading = true; reader.dlDone = done; reader.dlTotal = total
        }
        function onFinished(cid) {
            if (cid !== reader.curChapterId) return
            reader.downloading = false; reader.errorMsg = ""; reader.load()
        }
        function onFailed(cid, reason) {
            if (cid !== reader.curChapterId) return
            reader.downloading = false; reader.errorMsg = reason
        }
    }

    // --- chapter crossing (newest-first order) ---
    function openChapterById(id, atLast) {
        if (!id || !id.length) return
        pendingAtLast = !!atLast
        curChapterId = String(id)
    }
    function goNextChapter() {
        if (hasNewer) openChapterById(chapters[curIndex - 1].id, false)
        else if (chapters.length && curIndex === 0) atEnd = true
    }
    function goPrevChapter(atLast) {
        if (hasOlder) openChapterById(chapters[curIndex + 1].id, atLast)
    }

    // --- paged turning (direction-aware; crosses chapter at the ends) ---
    function turnNext() {
        if (!max) return
        if (isDouble) { var nx = Engine.stepNext(page - 1, ctx()); if (nx === null) goNextChapter(); else page = nx + 1 }
        else if (page < max) page = page + 1
        else goNextChapter()
    }
    function turnPrev() {
        if (!max) { goPrevChapter(true); return }
        if (isDouble) { var pv = Engine.stepPrev(page - 1, ctx()); if (pv === null) goPrevChapter(true); else page = pv + 1 }
        else if (page > 1) page = page - 1
        else goPrevChapter(true)
    }
    // HUD prev/next: paged turns a page; long_strip jumps chapters
    function prevAction() { paged ? turnPrev() : goPrevChapter(false) }
    function nextAction() { paged ? turnNext() : goNextChapter() }

    function setStyle(s) {
        if (s === style) return
        var keep = page
        prefs.reading_style = s
        if ((s === "double_page" || s === "double_page_v2") && max)
            page = Engine.snapTwoPageIndex(keep - 1, ctx()) + 1
    }

    // Smooth long-strip scrolling (wheel + click + edge bars): animate contentY toward
    // an accumulating target so rapid wheel notches glide instead of stepping harshly.
    property real _scrollTarget: 0
    NumberAnimation { id: scrollAnim; target: flick; property: "contentY"
        duration: 240; easing.type: Easing.OutCubic }
    function smoothScrollBy(dy) {
        var hmax = Math.max(0, flick.contentHeight - flick.height)
        var base = scrollAnim.running ? reader._scrollTarget : flick.contentY
        var t = Math.max(0, Math.min(hmax, base + dy))
        reader._scrollTarget = t
        scrollAnim.stop(); scrollAnim.from = flick.contentY; scrollAnim.to = t; scrollAnim.start()
    }
    function smoothScrollTo(y) { smoothScrollBy(y - flick.contentY) }

    // ===================== auto-hide chrome (Tankoban Max behavior) =====================
    // HUD + side bars recede while reading (after 3s idle) and STAY hidden while you read.
    // They return when you reach for them — the cursor enters the top/bottom 60px edge — or
    // on wheel / click / hovering the HUD. Keyboard scrolling does NOT wake them (immersive).
    // A modal/dropdown open or hovering the HUD freezes them shown; "Pin toolbar" pins them on.
    property bool hudShown: true
    property bool hudHover: false
    property bool edgeCooldown: false                 // brief lock after an edge-reveal (anti-flicker)
    readonly property int hudEdgePx: 60               // reveal band at top/bottom
    readonly property bool pinned: prefs.sticky_top_nav
    readonly property bool frozen: showPrefs || showJump || showChapters || hudMenu !== "" || hudHover
    readonly property bool chromeShown: hudShown || pinned || frozen
    Timer { id: idleHide; interval: 3000; running: reader.max > 0
        onTriggered: if (!reader.frozen && !reader.pinned) reader.hudShown = false }
    Timer { id: edgeCool; interval: 600; onTriggered: reader.edgeCooldown = false }
    function pokeChrome() { hudShown = true; if (!pinned) idleHide.restart() }
    onFrozenChanged: { if (frozen) { idleHide.stop(); hudShown = true } else pokeChrome() }

    // ===================== visual tree =====================
    focus: true
    // Keyboard scroll/nav — Tankoban Max key map. Scroll/turn keys deliberately do NOT pokeChrome():
    // keyboard reading keeps the HUD hidden (immersive). Only Esc + modal handling are exempt.
    Keys.onPressed: (e) => {
        // Esc — close an open modal/menu, else leave the reader
        if (e.key === Qt.Key_Escape) {
            if (showPrefs || showJump || showChapters || hudMenu !== "") {
                showPrefs = false; showJump = false; showChapters = false; hudMenu = ""
            } else reader.backRequested()
            e.accepted = true
            return
        }
        // never scroll / turn pages behind an open modal or dropdown
        if (showPrefs || showJump || showChapters || hudMenu !== "") return
        if (reader.max <= 0) return

        if (reader.style === "long_strip") {
            var shift  = (e.modifiers & Qt.ShiftModifier) !== 0
            var step   = Math.max(64, flick.height * 0.12)     // arrow
            var big    = Math.max(64, flick.height * 0.25)     // shift+arrow
            var screen = flick.height * 0.90                   // space / page
            var hmax   = Math.max(0, flick.contentHeight - flick.height)
            switch (e.key) {
            case Qt.Key_Down:     reader.smoothScrollBy(shift ? big : step);    e.accepted = true; break
            case Qt.Key_Up:       reader.smoothScrollBy(shift ? -big : -step);   e.accepted = true; break
            case Qt.Key_Space:    reader.smoothScrollBy(shift ? -screen : screen); e.accepted = true; break
            case Qt.Key_PageDown: reader.smoothScrollBy(screen);   e.accepted = true; break
            case Qt.Key_PageUp:   reader.smoothScrollBy(-screen);  e.accepted = true; break
            case Qt.Key_Home:     reader.smoothScrollTo(0);        e.accepted = true; break
            case Qt.Key_End:      reader.smoothScrollTo(hmax);     e.accepted = true; break
            }
            return
        }

        // paged (single_page / double_page) — Left/Right RTL-aware
        switch (e.key) {
        case Qt.Key_Left:     (rtl ? turnNext : turnPrev)(); e.accepted = true; break
        case Qt.Key_Right:    (rtl ? turnPrev : turnNext)(); e.accepted = true; break
        case Qt.Key_Space:    ((e.modifiers & Qt.ShiftModifier) ? turnPrev : turnNext)(); e.accepted = true; break
        case Qt.Key_PageDown: turnNext(); e.accepted = true; break
        case Qt.Key_PageUp:   turnPrev(); e.accepted = true; break
        case Qt.Key_Home:     reader.page = 1; e.accepted = true; break
        case Qt.Key_End:      reader.page = reader.max; e.accepted = true; break
        }
    }

    Rectangle { anchors.fill: parent; color: prefs.dark_background ? "#000000" : "#0a0b10" }

    // ── the page surface (scrolls) ──
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: pageCol ? pageCol.height : height
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        pixelAligned: true
        interactive: reader.max > 0 && reader.style === "long_strip"
        opacity: reader.max > 0 ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        // Smooth wheel scrolling for long-strip (default Flickable wheel steps harshly).
        WheelHandler {
            enabled: reader.style === "long_strip" && reader.max > 0
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (e) => { reader.pokeChrome(); reader.smoothScrollBy(-e.angleDelta.y * 1.4) }
        }

        // LONG STRIP
        Column {
            id: pageCol
            visible: reader.style === "long_strip"
            width: Math.min(flick.width, flick.width * reader.portraitWidthPct / 100)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: prefs.gap ? 8 : 0
            Repeater {
                model: reader.style === "long_strip" ? reader.pagesModel : []
                delegate: Image {
                    required property var modelData
                    required property int index
                    width: pageCol.width
                    height: (implicitWidth > 0) ? width * (implicitHeight / implicitWidth) : width * 1.45
                    fillMode: Image.PreserveAspectFit
                    source: modelData.url
                    asynchronous: true; cache: true; smooth: true; mipmap: true
                    sourceSize.width: 1100
                    onStatusChanged: if (status === Image.Ready) reader.reportDims(index, implicitWidth, implicitHeight)
                }
            }
        }

        // SINGLE PAGE
        Item {
            visible: reader.style === "single_page"
            width: flick.width; height: flick.height
            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width * reader.portraitWidthPct / 100
                height: reader.fit === "height" ? parent.height : ((implicitWidth > 0) ? width * (implicitHeight / implicitWidth) : width * 1.45)
                fillMode: Image.PreserveAspectFit
                source: reader.curUrl
                asynchronous: true; cache: true; smooth: true; mipmap: true
                sourceSize.width: 1400
                onStatusChanged: if (status === Image.Ready) reader.reportDims(reader.page - 1, implicitWidth, implicitHeight)
            }
        }

        // DOUBLE PAGE (double_page + double_page_v2)
        Item {
            id: dbl
            visible: reader.isDouble && reader.pair !== null
            width: flick.width; height: flick.height
            property var layout: (reader.isDouble && reader.pair) ? Engine.computeSpreadLayout({
                kind: reader.pair.kind,
                anchorDims: reader.dims[reader.pair.anchorIndex] || { w: 800, h: 1200 },
                partnerDims: reader.pair.partnerIndex !== null ? (reader.dims[reader.pair.partnerIndex] || { w: 800, h: 1200 }) : null,
                containerW: flick.width, containerH: flick.height, gutter: 0, fitWidth: true, rtl: reader.rtl
            }) : null
            Image { visible: false; asynchronous: true; cache: true
                source: (reader.pair && reader.pair.anchorIndex < reader.max) ? reader.pagesModel[reader.pair.anchorIndex].url : ""
                onStatusChanged: if (status === Image.Ready && reader.pair) reader.reportDims(reader.pair.anchorIndex, implicitWidth, implicitHeight) }
            Image { visible: false; asynchronous: true; cache: true
                source: (reader.pair && reader.pair.partnerIndex !== null && reader.pair.partnerIndex < reader.max) ? reader.pagesModel[reader.pair.partnerIndex].url : ""
                onStatusChanged: if (status === Image.Ready && reader.pair && reader.pair.partnerIndex !== null) reader.reportDims(reader.pair.partnerIndex, implicitWidth, implicitHeight) }
            Row {
                anchors.centerIn: parent
                spacing: 0
                Repeater {
                    model: dbl.layout ? dbl.layout.pages : []
                    delegate: Image {
                        required property var modelData
                        property int pgIdx: modelData.role === "anchor" ? reader.pair.anchorIndex : reader.pair.partnerIndex
                        width: modelData.w; height: modelData.h
                        fillMode: Image.PreserveAspectFit
                        source: (pgIdx >= 0 && pgIdx < reader.max) ? reader.pagesModel[pgIdx].url : ""
                        asynchronous: true; cache: true; smooth: true; mipmap: true
                    }
                }
            }
        }
    }

    // ── click zones: left/right thirds turn (paged) or scroll (strip) ──
    MouseArea {
        anchors.fill: parent
        enabled: reader.max > 0 && !reader.atEnd
        acceptedButtons: Qt.LeftButton
        onClicked: (m) => {
            reader.pokeChrome()
            var third = width / 3
            if (reader.style === "long_strip") {
                if (m.x < third) reader.smoothScrollBy(-flick.height * 0.82)
                else if (m.x > width - third) reader.smoothScrollBy(flick.height * 0.82)
            } else {
                if (m.x < third) (reader.rtl ? reader.turnNext : reader.turnPrev)()
                else if (m.x > width - third) (reader.rtl ? reader.turnPrev : reader.turnNext)()
            }
        }
    }

    // reveal-on-EDGE overlay (Max behavior): NoButton so it never eats page-turn clicks.
    // Only wakes the HUD when it's currently hidden AND the cursor reaches the top/bottom
    // 60px band — so mid-screen movement while reading leaves the chrome hidden. A 600ms
    // cooldown after a reveal prevents flicker when the cursor lingers at the edge.
    MouseArea {
        anchors.fill: parent; z: 18
        enabled: reader.max > 0
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onPositionChanged: (m) => {
            if (reader.chromeShown || reader.edgeCooldown) return
            if (m.y <= reader.hudEdgePx || m.y >= height - reader.hudEdgePx) {
                reader.pokeChrome(); reader.edgeCooldown = true; edgeCool.restart()
            }
        }
    }

    // ── download panel (no local pages; download-fed, never streams) ──
    Column {
        visible: reader.max === 0
        anchors.centerIn: parent; spacing: 16; width: parent.width * 0.7
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: reader.seriesTitle; color: theme.ink; font.family: theme.display; font.pixelSize: 26
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: reader.curLabel; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14 }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
            visible: reader.errorMsg.length > 0
            text: reader.errorMsg; color: "#e6a3a3"; font.family: theme.ui; font.pixelSize: 13; wrapMode: Text.WordWrap }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
            visible: !reader.downloading && reader.errorMsg.length === 0
            text: "Not downloaded yet — download this chapter to read it offline."
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; wrapMode: Text.WordWrap }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
            visible: reader.downloading
            text: reader.dlTotal > 0 ? ("Downloading… " + reader.dlDone + " / " + reader.dlTotal + " pages") : "Starting download…"
            color: theme.gold; font.family: theme.ui; font.pixelSize: 14 }
        Rectangle {
            visible: reader.downloading && reader.dlTotal > 0
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width * 0.6; height: 6; radius: 3; color: theme.glassTint
            Rectangle { height: parent.height; radius: 3; color: theme.gold
                width: parent.width * (reader.dlTotal > 0 ? reader.dlDone / reader.dlTotal : 0)
                Behavior on width { NumberAnimation { duration: 200 } } }
        }
        Rectangle {
            visible: !reader.downloading
            anchors.horizontalCenter: parent.horizontalCenter
            radius: 10; height: 40; width: dlt.implicitWidth + 40
            color: dlMa.containsMouse ? Qt.lighter(theme.gold, 1.1) : theme.gold
            Text { id: dlt; anchors.centerIn: parent
                text: reader.errorMsg.length ? "Retry download" : "⬇  Download chapter"
                color: "#1a1306"; font.family: theme.ui; font.weight: Font.DemiBold; font.pixelSize: 14 }
            MouseArea { id: dlMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: reader.startDownload() }
        }
    }

    // back affordance for the download panel (a real sized button, top-left)
    Rectangle {
        visible: reader.max === 0
        z: 100
        anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 18
        width: bkRow.implicitWidth + 28; height: 40; radius: 10
        color: bkMa.containsMouse ? theme.glassHi : theme.glassTint
        border.width: 1; border.color: theme.edge
        Row {
            id: bkRow; anchors.centerIn: parent; spacing: 7
            Text { text: "‹"; color: bkMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.display; font.pixelSize: 22; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Back"; color: bkMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
        }
        MouseArea { id: bkMa; anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor; onClicked: reader.backRequested() }
    }

    // ── "all caught up" end card ──
    Column {
        visible: reader.atEnd
        anchors.centerIn: parent; spacing: 16; width: parent.width * 0.6
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: "You're all caught up"
            color: theme.ink; font.family: theme.display; font.pixelSize: 28 }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            text: "You've reached the latest chapter. Check back later for more."
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14 }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter; spacing: 12
            Rectangle { radius: 9; height: 38; width: stayT.implicitWidth + 30; color: staMa.containsMouse ? theme.glassHi : theme.glassTint
                border.width: 1; border.color: theme.edge
                Text { id: stayT; anchors.centerIn: parent; text: "‹ Stay here"; color: theme.ink; font.family: theme.ui; font.pixelSize: 13 }
                MouseArea { id: staMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: reader.atEnd = false } }
            Rectangle { radius: 9; height: 38; width: bsT.implicitWidth + 30; color: theme.gold
                Text { id: bsT; anchors.centerIn: parent; text: "Back to series"; color: "#1a1306"; font.family: theme.ui; font.weight: Font.DemiBold; font.pixelSize: 13 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: reader.backRequested() } }
        }
    }

    // ── edge side-bars (prev/next or scroll) — auto-hide ──
    component NavBar: Rectangle {
        property bool isLeft: true
        property bool shown: true
        enabled: shown
        width: 52; height: parent.height
        color: navMa.containsMouse ? Qt.rgba(1,1,1,0.06) : "transparent"
        opacity: shown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 140 } }
        Text { anchors.centerIn: parent; text: parent.isLeft ? "‹" : "›"
            color: navMa.containsMouse ? theme.gold : Qt.rgba(1,1,1,0.45)
            font.family: theme.display; font.pixelSize: 34 }
        MouseArea {
            id: navMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (reader.style === "long_strip") reader.smoothScrollBy(parent.isLeft ? -flick.height * 0.82 : flick.height * 0.82)
                else (parent.isLeft ? (reader.rtl ? reader.turnNext : reader.turnPrev)
                                    : (reader.rtl ? reader.turnPrev : reader.turnNext))()
            }
        }
    }
    NavBar { isLeft: true;  anchors.left: parent.left;   visible: reader.max > 0 && !reader.atEnd; shown: reader.chromeShown; z: 15 }
    NavBar { isLeft: false; anchors.right: parent.right; visible: reader.max > 0 && !reader.atEnd; shown: reader.chromeShown; z: 15 }

    // floating back-to-top (long strip)
    Rectangle {
        visible: reader.max > 0 && reader.style === "long_strip" && prefs.back_to_top && !reader.atEnd
        z: 16; width: 44; height: 44; radius: 22
        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 24
        color: ttMa.containsMouse ? theme.gold : theme.glassTint; border.width: 1; border.color: theme.edge
        Text { anchors.centerIn: parent; text: "↑"; color: ttMa.containsMouse ? "#1a1306" : theme.ink
            font.family: theme.display; font.pixelSize: 20 }
        MouseArea { id: ttMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: reader.smoothScrollTo(0) }
    }

    // ===================== HUD (frosted glass, top) =====================
    Glass {
        id: hud
        backdrop: reader.backdrop
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        anchors.margins: 10
        height: 52; radius: 14
        visible: reader.max > 0 && !reader.atEnd
        opacity: reader.chromeShown ? 1 : 0
        enabled: reader.chromeShown
        Behavior on opacity { NumberAnimation { duration: 180 } }
        z: 20
        HoverHandler { onHoveredChanged: reader.hudHover = hovered }

        // left cluster: back · series · chapter chip
        Row {
            anchors.left: parent.left; anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter; spacing: 14
            Text { text: "‹"; color: backMa.containsMouse ? theme.gold : theme.ink; font.family: theme.display
                font.pixelSize: 24; anchors.verticalCenter: parent.verticalCenter
                MouseArea { id: backMa; anchors.fill: parent; anchors.margins: -8; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: reader.backRequested() } }
            Text { text: reader.seriesTitle; color: theme.ink; font.family: theme.display; font.weight: Font.DemiBold
                font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter }
            // chapter chip → chapter modal
            Rectangle { anchors.verticalCenter: parent.verticalCenter; radius: 8; height: 28
                width: chRow.implicitWidth + 22; color: chMa.containsMouse ? theme.glassHi : theme.glassTint
                border.width: 1; border.color: chMa.containsMouse ? Qt.rgba(0.94,0.77,0.29,0.55) : theme.edge
                Row { id: chRow; anchors.centerIn: parent; spacing: 6
                    Text { text: "≣"; color: theme.gold; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: reader.curLabel; color: theme.ink; font.family: theme.ui; font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter } }
                MouseArea { id: chMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.showChapters = true } }
        }

        // right cluster
        Row {
            anchors.right: parent.right; anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter; spacing: 10
            // prev
            Text { text: "Prev"; color: pvMa.containsMouse ? theme.gold : theme.inkDim; font.family: theme.ui
                font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter
                MouseArea { id: pvMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: reader.prevAction() } }
            // page chip → page-jump modal
            Rectangle { anchors.verticalCenter: parent.verticalCenter; radius: 8; height: 28
                width: pc.implicitWidth + 20; color: pgMa.containsMouse ? theme.glassHi : theme.glassTint
                border.width: 1; border.color: theme.edge
                Text { id: pc; anchors.centerIn: parent; text: reader.page + " / " + reader.max
                    color: theme.gold; font.family: theme.display; font.pixelSize: 13; font.weight: Font.DemiBold }
                MouseArea { id: pgMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.showJump = true } }
            // next
            Text { text: "Next"; color: nxMa.containsMouse ? theme.gold : theme.inkDim; font.family: theme.ui
                font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter
                MouseArea { id: nxMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: reader.nextAction() } }
            Rectangle { width: 1; height: 22; color: theme.edge; anchors.verticalCenter: parent.verticalCenter }

            // reading-mode dropdown trigger
            Rectangle { anchors.verticalCenter: parent.verticalCenter; radius: 8; height: 28
                width: mdRow.implicitWidth + 18; color: mdMa.containsMouse || reader.hudMenu === "mode" ? theme.glassHi : theme.glassTint
                border.width: 1; border.color: theme.edge
                Row { id: mdRow; anchors.centerIn: parent; spacing: 6
                    Text { text: reader.modeShort(reader.style); color: theme.ink; font.family: theme.ui; font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "▾"; color: theme.inkDim; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter } }
                MouseArea { id: mdMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.hudMenu = (reader.hudMenu === "mode" ? "" : "mode") } }

            // direction toggle (locked LTR in MangaPlus)
            Rectangle { anchors.verticalCenter: parent.verticalCenter; radius: 8; height: 28; width: dt.implicitWidth + 18
                color: dMa.containsMouse ? theme.glassHi : theme.glassTint; border.width: 1; border.color: theme.edge
                opacity: reader.style === "double_page_v2" ? 0.5 : 1
                Text { id: dt; anchors.centerIn: parent; text: reader.rtl ? "RTL" : "LTR"
                    color: theme.ink; font.family: theme.ui; font.pixelSize: 12 }
                MouseArea { id: dMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    enabled: reader.style !== "double_page_v2"
                    onClicked: prefs.reading_direction = (reader.rtl ? "left_right" : "right_left") } }

            // change-pairing (double modes)
            Text { visible: reader.isDouble; text: "⇄"; font.pixelSize: 18
                color: reader.couplingNudge ? theme.gold : (swMa.containsMouse ? theme.gold : theme.inkDim)
                anchors.verticalCenter: parent.verticalCenter
                MouseArea { id: swMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { var n = reader.couplingNudge ? 0 : 1; reader.couplingNudge = n
                        reader.page = Engine.snapTwoPageIndex(reader.page - 1,
                            { n: reader.max, isSpreadAt: function (i) { var d = reader.dims[i]; return d ? Engine.isSpread(d.w, d.h) : false }, couplingNudge: n }) + 1 } } }

            // width dropdown trigger (single/strip)
            Rectangle { visible: reader.style === "single_page" || reader.style === "long_strip"
                anchors.verticalCenter: parent.verticalCenter; radius: 8; height: 28
                width: wdRow.implicitWidth + 16; color: wdMa.containsMouse || reader.hudMenu === "width" ? theme.glassHi : theme.glassTint
                border.width: 1; border.color: theme.edge
                Row { id: wdRow; anchors.centerIn: parent; spacing: 4
                    Text { text: reader.portraitWidthPct + "%"; color: theme.ink; font.family: theme.ui; font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "▾"; color: theme.inkDim; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter } }
                MouseArea { id: wdMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.hudMenu = (reader.hudMenu === "width" ? "" : "width") } }

            // settings (prefs modal)
            Text { text: "⚙"; color: grMa.containsMouse ? theme.gold : theme.inkDim; font.pixelSize: 18
                anchors.verticalCenter: parent.verticalCenter
                MouseArea { id: grMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: reader.showPrefs = true } }
            Rectangle { width: 1; height: 22; color: theme.edge; anchors.verticalCenter: parent.verticalCenter }
            // window controls
            Image { source: "../assets/icons/minimize.svg"; sourceSize.width: 18; sourceSize.height: 18
                width: 18; height: 18; fillMode: Image.PreserveAspectFit; anchors.verticalCenter: parent.verticalCenter
                opacity: miMa.containsMouse ? 1 : 0.7
                MouseArea { id: miMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.minimizeRequested() } }
            Image { source: "../assets/icons/power.svg"; sourceSize.width: 18; sourceSize.height: 18
                width: 18; height: 18; fillMode: Image.PreserveAspectFit; anchors.verticalCenter: parent.verticalCenter
                opacity: poMa.containsMouse ? 1 : 0.7
                MouseArea { id: poMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: reader.closeRequested() } }
        }
    }

    // mode short labels
    function modeShort(s) {
        if (s === "long_strip") return "Strip"
        if (s === "single_page") return "Single"
        if (s === "double_page") return "Double"
        if (s === "double_page_v2") return "MangaPlus"
        return "Mode"
    }

    // ── HUD dropdown menus (mode / width) ──
    Rectangle {
        visible: reader.hudMenu === "mode"
        z: 30; radius: 10; color: "#15171f"; border.width: 1; border.color: theme.edge
        anchors.top: hud.bottom; anchors.topMargin: 4; anchors.right: parent.right; anchors.rightMargin: 140
        width: 190; height: modeCol.height + 12
        Column { id: modeCol; width: parent.width; y: 6
            Repeater {
                model: [{v:"long_strip",t:"Long Strip"},{v:"single_page",t:"Single Page"},
                        {v:"double_page",t:"Double Page"},{v:"double_page_v2",t:"Double Page (MangaPlus)"}]
                delegate: Rectangle {
                    required property var modelData
                    width: parent.width; height: 36; color: mi.containsMouse ? theme.glassHi : "transparent"
                    Text { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter
                        text: modelData.t; color: reader.style === modelData.v ? theme.gold : theme.ink
                        font.family: theme.ui; font.pixelSize: 13 }
                    Text { visible: reader.style === modelData.v; anchors.right: parent.right; anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter; text: "✓"; color: theme.gold; font.pixelSize: 13 }
                    MouseArea { id: mi; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { reader.setStyle(modelData.v); reader.hudMenu = "" } }
                }
            }
        }
    }
    Rectangle {
        visible: reader.hudMenu === "width"
        z: 30; radius: 10; color: "#15171f"; border.width: 1; border.color: theme.edge
        anchors.top: hud.bottom; anchors.topMargin: 4; anchors.right: parent.right; anchors.rightMargin: 60
        width: 92; height: wCol.height + 12
        Column { id: wCol; width: parent.width; y: 6
            Repeater {
                model: [50, 60, 70, 74, 78, 90, 100]
                delegate: Rectangle {
                    required property var modelData
                    width: parent.width; height: 32; color: wi.containsMouse ? theme.glassHi : "transparent"
                    Text { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter
                        text: modelData + "%"; color: reader.portraitWidthPct === modelData ? theme.gold : theme.ink
                        font.family: theme.ui; font.pixelSize: 13 }
                    MouseArea { id: wi; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { prefs.portrait_width_pct = modelData; reader.hudMenu = "" } }
                }
            }
        }
    }
    // dismiss a HUD dropdown on outside click
    MouseArea { anchors.fill: parent; z: 29; visible: reader.hudMenu !== ""
        onClicked: reader.hudMenu = "" }

    // ===================== MODALS =====================
    component ModalScrim: Rectangle {
        anchors.fill: parent; color: Qt.rgba(0,0,0,0.62); z: 40
    }
    component ModalCard: Rectangle {
        anchors.centerIn: parent; z: 41; radius: 16; color: "#15171f"
        border.width: 1; border.color: theme.edge
    }

    // ── PREFERENCES ──
    ModalScrim { visible: reader.showPrefs; MouseArea { anchors.fill: parent; onClicked: reader.showPrefs = false } }
    ModalCard {
        visible: reader.showPrefs
        width: 420; height: prefCol.height + 40
        Column {
            id: prefCol; width: parent.width - 48; x: 24; y: 24; spacing: 6
            Text { text: "⚙  Preferences"; color: theme.ink; font.family: theme.display; font.pixelSize: 18
                bottomPadding: 8 }
            PrefToggle { label: "Pin toolbar"; checked: prefs.sticky_top_nav; onToggled: (v) => prefs.sticky_top_nav = v }
            PrefToggle { label: "Gap between pages"; checked: prefs.gap; onToggled: (v) => prefs.gap = v }
            PrefToggle { label: "Back-to-top button"; checked: prefs.back_to_top; onToggled: (v) => prefs.back_to_top = v }
            PrefToggle { label: "Dark background"; checked: prefs.dark_background; onToggled: (v) => prefs.dark_background = v }
            Item { width: 1; height: 8 }
            Text { text: "IMAGE FIT"; color: theme.inkDimmer; font.family: theme.display; font.pixelSize: 11; font.letterSpacing: 2 }
            PrefRadio { label: "Fit width"; checked: prefs.image_fit === "width"; onPicked: prefs.image_fit = "width" }
            PrefRadio { label: "Fit height"; checked: prefs.image_fit === "height"; onPicked: prefs.image_fit = "height" }
        }
    }

    // ── CHAPTER SELECT ──
    ModalScrim { visible: reader.showChapters; MouseArea { anchors.fill: parent; onClicked: reader.showChapters = false } }
    ModalCard {
        visible: reader.showChapters
        width: 520; height: Math.min(parent.height * 0.72, 560)
        Text { id: chTitle; text: "≣  Select Chapter"; color: theme.ink; font.family: theme.display; font.pixelSize: 18
            x: 24; y: 22 }
        GridView {
            anchors.top: chTitle.bottom; anchors.topMargin: 16
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.leftMargin: 20; anchors.rightMargin: 20; anchors.bottomMargin: 20
            clip: true; cellWidth: 96; cellHeight: 44
            model: reader.chapters
            delegate: Item {
                required property var modelData
                width: 96; height: 44
                Rectangle {
                    anchors.fill: parent; anchors.margins: 4; radius: 8
                    property bool active: String(modelData.id) === reader.curChapterId
                    color: active ? theme.gold : (cgMa.containsMouse ? theme.glassHi : theme.glassTint)
                    border.width: 1; border.color: active ? theme.gold : theme.edge
                    Text { anchors.centerIn: parent
                        text: (modelData.number !== undefined && modelData.number !== "") ? modelData.number
                              : (modelData.name || "?")
                        color: parent.active ? "#1a1306" : theme.ink; font.family: theme.ui; font.pixelSize: 13
                        elide: Text.ElideRight; width: parent.width - 12; horizontalAlignment: Text.AlignHCenter }
                    MouseArea { id: cgMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { reader.showChapters = false
                            if (String(modelData.id) !== reader.curChapterId) reader.openChapterById(modelData.id, false) } }
                }
            }
        }
    }

    // ── PAGE JUMP ──
    ModalScrim { visible: reader.showJump; MouseArea { anchors.fill: parent; onClicked: reader.showJump = false } }
    ModalCard {
        visible: reader.showJump
        width: 520; height: Math.min(parent.height * 0.72, 560)
        Text { id: pgTitle; text: "Select Page"; color: theme.ink; font.family: theme.display; font.pixelSize: 18; x: 24; y: 22 }
        GridView {
            anchors.top: pgTitle.bottom; anchors.topMargin: 16
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.leftMargin: 20; anchors.rightMargin: 20; anchors.bottomMargin: 20
            clip: true; cellWidth: 60; cellHeight: 44
            model: reader.max
            delegate: Item {
                required property int index
                width: 60; height: 44
                Rectangle {
                    anchors.fill: parent; anchors.margins: 4; radius: 8
                    property bool active: index + 1 === reader.page
                    color: active ? theme.gold : (pgGMa.containsMouse ? theme.glassHi : theme.glassTint)
                    border.width: 1; border.color: active ? theme.gold : theme.edge
                    Text { anchors.centerIn: parent; text: index + 1
                        color: parent.active ? "#1a1306" : theme.ink; font.family: theme.ui; font.pixelSize: 13 }
                    MouseArea { id: pgGMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { reader.page = index + 1; reader.showJump = false
                            if (reader.style === "long_strip") flick.contentY = 0 } }
                }
            }
        }
    }

    // ── reusable pref controls ──
    component PrefToggle: Item {
        id: ptRoot
        property string label: ""
        property bool checked: false
        signal toggled(bool v)
        width: parent ? parent.width : 360; height: 40
        Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
            text: ptRoot.label; color: theme.ink; font.family: theme.ui; font.pixelSize: 14 }
        Rectangle {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            width: 42; height: 24; radius: 12
            color: ptRoot.checked ? theme.gold : theme.glassTint; border.width: 1; border.color: theme.edge
            Rectangle { width: 18; height: 18; radius: 9; color: "#ffffff"; y: 3
                x: ptRoot.checked ? parent.width - width - 3 : 3
                Behavior on x { NumberAnimation { duration: 120 } } }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
            onClicked: ptRoot.toggled(!ptRoot.checked) }
    }
    component PrefRadio: Item {
        id: prRoot
        property string label: ""
        property bool checked: false
        signal picked()
        width: parent ? parent.width : 360; height: 34
        Row { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; spacing: 10
            Rectangle { width: 18; height: 18; radius: 9; color: "transparent"; border.width: 2
                border.color: prRoot.checked ? theme.gold : theme.edge; anchors.verticalCenter: parent.verticalCenter
                Rectangle { anchors.centerIn: parent; width: 9; height: 9; radius: 5; color: theme.gold; visible: prRoot.checked } }
            Text { text: prRoot.label; color: theme.ink; font.family: theme.ui; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter } }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: prRoot.picked() }
    }
}

```


### qml/BookReader.qml

```
// BookReader.qml — Tankoban 2's foliate EPUB reader, brought home into Colosseum
// via a QML WebEngineView (the whole reason for the MSVC migration). Loads
// resources/book_reader/ebook_reader.html, wires the native `BookBridge` over
// QWebChannel (qwebchannel.js + qt_bridge_shim.js injected at DocumentCreation,
// so the file:// page can reach the bridge), then opens a downloaded .epub via
// window.__ebookOpenBook(path). The book file comes from the download-fed
// `Books` engine — never a stream.
import QtQuick
import QtWebEngine
import QtWebChannel

Item {
    id: reader
    property string bookPath: ""
    property var bookMeta: ({})     // {id, title, cover, c1, c2, book} for the Continue card
    property bool ready: false

    signal closed()              // BACK to the library (Esc / reader close)
    signal minimizeRequested()   // foliate chrome minimize → Colosseum window

    // Register the native bridge under the name "bridge" (what qt_bridge_shim.js
    // expects as channel.objects.bridge). registerObject by name works for a C++
    // context object; registeredObjects:[...] needs a QML attached id it can't carry.
    Component.onCompleted: bridgeChannel.registerObject("bridge", BookBridge)

    // Open a local book file. First call loads the reader HTML, then fires
    // __ebookOpenBook on load-success; later calls (page already up) fire it directly.
    function open(path, book) {
        reader.bookPath = path
        reader.bookMeta = book || ({})
        reader.ready = false
        watchdog.restart()
        if (web.loadProgress >= 100 && web.url != "") reader.openInPage()
        else web.url = Qt.resolvedUrl("../resources/book_reader/ebook_reader.html")
    }
    function openInPage() {
        if (reader.bookPath === "") return
        var esc = reader.bookPath.replace(/\\/g, "\\\\").replace(/'/g, "\\'")
        web.runJavaScript("window.__ebookOpenBook('" + esc + "')")
    }

    Rectangle { anchors.fill: parent; color: "#000000" }

    WebEngineView {
        id: web
        anchors.fill: parent
        backgroundColor: "#000000"
        settings.localContentCanAccessFileUrls: true
        settings.localContentCanAccessRemoteUrls: true
        settings.javascriptCanAccessClipboard: true

        // The bridge scripts (qwebchannel.js + qt_bridge_shim.js) are declared in
        // ebook_reader.html's <head>; this webChannel just provides the transport.
        // The "bridge" object is registered by name in Component.onCompleted above.
        webChannel: WebChannel { id: bridgeChannel }

        onLoadingChanged: function (info) {
            if (info.status === WebEngineView.LoadSucceededStatus) reader.openInPage()
        }
        onJavaScriptConsoleMessage: function (level, message, line, src) {
            console.log("[BookReader JS] " + message)
        }
    }

    // Black overlay until foliate's `stabilized` fires (BookBridge.readerReady).
    Rectangle {
        anchors.fill: parent; color: "#000000"; visible: !reader.ready
        Text {
            anchors.centerIn: parent; text: "Loading…"
            color: Qt.rgba(1, 1, 1, 0.55); font.pixelSize: 14
        }
    }

    // Watchdog: if `stabilized` never arrives, reveal the reader anyway after 6s.
    Timer { id: watchdog; interval: 6000; onTriggered: reader.ready = true }

    Connections {
        target: BookBridge
        function onReaderReady() { reader.ready = true }
        function onCloseRequested() { reader.closed() }
        function onWindowCloseRequested() { reader.closed() }
        function onWindowMinimizeRequested() { reader.minimizeRequested() }
        function onFullscreenRequested(on) { /* Colosseum is always fullscreen */ }
        // Feed the unified Continue/resume row (download-fed reading, like manga).
        function onProgressSaved(bookId, fraction) {
            if (typeof Progress === "undefined" || reader.bookPath === "") return
            var m = reader.bookMeta || ({})
            var idStr = (m.id !== undefined && ("" + m.id).length) ? ("" + m.id) : reader.bookPath
            Progress.record({
                "id": idStr,
                "kind": "book",
                "caption": m.title || "",
                "title": m.title || "",
                "sub": (fraction > 0 ? Math.round(fraction * 100) + "%" : "Reading"),
                "cover": m.cover || "",
                "c1": m.c1 !== undefined ? m.c1 : "#2a2440",
                "c2": m.c2 !== undefined ? m.c2 : "#15111f",
                "progress": Math.min(1, Math.max(0, fraction)),
                "resume": { "path": reader.bookPath, "book": m }
            })
        }
    }
}

```

