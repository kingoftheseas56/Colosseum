// ExtensionsPage — the extension store: one page where Stremio-protocol addons
// are discovered, browsed, installed, toggled, ordered and removed.
// Ratified design: agents/colosseum-extensions-mock.html (2026-07-05, "we can go
// for it"), spec: docs/superpowers/specs/2026-07-05-colosseum-extensions-store-design.md.
// Serves all three worlds, all live as of stage 1a — Tankoban and Biblio carry real
// catalogues and wells now. Data = `Extensions` (the C++ registry)
// + ExtensionsCatalog.js (curated rails, community registry, adult wall).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "ExtensionsCatalog.js" as Catalog

Item {
    id: root
    property Item backdrop: null
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal searchClicked()

    Theme { id: theme }

    // ---- registry bindings ----
    property var installedList: []

    // Global Explicit Content preference, injected live by Main.qml (same one Discover,
    // genres and search read). Off -> the registry list is asked filtered and adult
    // entries are dropped; on -> the catalogue is unfiltered and anything is installable.
    property bool showExplicit: false
    onShowExplicitChanged: if (communityLoaded || communityLoading) loadCommunity()

    // ---- worlds (spec §3.2): derived from each manifest's own `types`, never stored.
    //      One extension can serve two worlds — Torrent Indexers feeds comics AND books
    //      AND audiobooks from a single install — so these are filters, not partitions.
    function inWorld(entry, world) { return Catalog.inWorld(entry, world) }
    function installedIn(world) {
        var out = [];
        for (var i = 0; i < installedList.length; i++)
            if (Catalog.inWorld(installedList[i], world)) out.push(installedList[i]);
        return out;
    }
    function countIn(world) { return installedIn(world).length }

    // Grouped by job: Catalogue (what fills the shelves) then Wells (what fetches),
    // then anything that is neither — e.g. Anime Kitsu, a non-core catalog provider,
    // and OpenSubtitles, which answers subtitles only.
    function installedRowsFor(world) {
        var rows = installedIn(world), cat = [], wells = [], other = [];
        for (var i = 0; i < rows.length; i++) {
            if (Catalog.isCatalogue(rows[i])) cat.push(rows[i]);
            else if (Catalog.isWell(rows[i])) wells.push(rows[i]);
            else other.push(rows[i]);
        }
        return cat.concat(wells).concat(other);
    }

    readonly property var worldTitles: ({ theatre: "Theatre", tankoban: "Tankoban",
                                          biblio: "Biblio", universes: "Universes" })
    // Removing a well that serves two worlds removes it from BOTH. Say which, by name,
    // and require a second press — an unnamed one-click removal would silently empty a
    // world the user wasn't looking at.
    property string pendingRemoveId: ""
    function askRemove(entry) {
        var ws = Catalog.worldsFor(entry);
        var name = (entry.manifest && entry.manifest.name) || entry.id;
        if (ws.length > 1 && pendingRemoveId !== entry.id) {
            var names = [];
            for (var i = 0; i < ws.length; i++) names.push(worldTitles[ws[i]] || ws[i]);
            pendingRemoveId = entry.id;
            notice = name + " feeds " + names.join(" and ")
                   + ". Removing it takes it out of both — press Remove again to confirm.";
            noticeTimer.restart();
            return;
        }
        pendingRemoveId = "";
        Extensions.remove(entry.id);
    }
    // Reorder a well within the world the user is actually looking at. The arrows are
    // world-relative and the stored array is global, so the destination has to be resolved
    // against this world's own well list — see Catalog.moveDestination for why a global
    // ±1 both failed to move Tankoban and silently reordered Biblio.
    // moveDestination returns { id, index } or null — `id` is not always the clicked row,
    // because a swap may be cheaper to perform by moving its neighbour instead.
    function moveWell(id, delta) {
        var m = Catalog.moveDestination(installedList, world, id, delta);
        if (m) Extensions.moveTo(m.id, m.index);
    }
    // A world's rank for a well is its index among that world's wells — which is how one
    // stored row ranks 4th in Tankoban and 2nd in Biblio without storing a rank at all.
    function wellRank(entry, world) {
        var w = installedIn(world), n = 0;
        for (var i = 0; i < w.length; i++) {
            if (Catalog.isWell(w[i])) n++;
            if (w[i].id === entry.id) return n;
        }
        return 0;
    }
    property var installedKeys: ({})          // id AND transportUrl → true
    property var pendingUrls: ({})            // url → true while an install is in flight
    property string notice: ""                // one quiet line for install results

    // ---- page state ----
    property string world: "theatre"          // "theatre" | "tankoban" | "biblio"
    property string pane: "sources"           // "sources" | "browse" | "installed"
    property string query: ""
    property string sort: "top"               // "top" | "new" | "rising"
    property var communityRows: []
    property bool communityLoading: false
    property bool communityLoaded: false

    function refresh() {
        if (typeof Extensions === "undefined") return;
        installedList = Extensions.installed();
        var keys = {};
        for (var i = 0; i < installedList.length; i++) {
            keys[installedList[i].id] = true;
            keys[installedList[i].transportUrl] = true;
        }
        installedKeys = keys;
    }
    function carried(item) {
        return installedKeys[item.id] === true || installedKeys[item.url] === true;
    }
    function configureUrl(rawUrl) {
        var raw = String(rawUrl || "").trim();
        if (!raw || /^colosseum:\/\//i.test(raw)) return "";
        var split = raw.length;
        var query = raw.indexOf("?");
        var hash = raw.indexOf("#");
        if (query >= 0 && query < split) split = query;
        if (hash >= 0 && hash < split) split = hash;
        var path = raw.slice(0, split).replace(/\/+$/, "");
        var suffix = raw.slice(split);
        if (/\/manifest\.json$/i.test(path))
            path = path.replace(/\/manifest\.json$/i, "/configure");
        else
            path += "/configure";
        return path + suffix;
    }
    // Is this row one the house locks? Read off the installed entry rather than trusted
    // from the curated data, because the curated rails carry no `core` field at all —
    // which is how the featured slab came to print "built-in" over a removable add-on.
    function coreOf(item) {
        for (var i = 0; i < installedList.length; i++)
            if (installedList[i].id === item.id) return installedList[i].core === true;
        return false;
    }
    function hit(name) {
        return !query.length || name.toLowerCase().indexOf(query.toLowerCase()) !== -1;
    }
    function installFromCard(item) {
        if (typeof Extensions === "undefined" || carried(item)) return;
        sheet.openForUrl(item.url);
    }
    function loadCommunity() {
        communityLoading = true;
        var mySort = sort, myQuery = query;
        var myExplicit = showExplicit;
        Catalog.browse(mySort, myQuery, function(rows) {
            // A stale answer is discarded, but the flag it set is NOT its to keep: the
            // newer request owns it, and if none is coming this must still fall to false
            // or Browse sits on "Asking the registry…" forever with the re-entry guard
            // (!communityLoading) refusing to try again. (A5's audit P0-1.)
            if (mySort !== root.sort || myQuery !== root.query || myExplicit !== root.showExplicit) {
                if (!queryDebounce.running) root.communityLoading = false;
                return;
            }
            root.communityRows = rows || [];
            root.communityLoading = false;
            root.communityLoaded = true;
        }, myExplicit);
    }

    Component.onCompleted: refresh()
    Connections {
        target: typeof Extensions !== "undefined" ? Extensions : null
        function onChanged() { root.refresh() }
        function onInstallFinished(id, name) {
            root.notice = name + " installed — it answers from the next ask on.";
            var p = {};
            for (var k in root.pendingUrls) p[k] = true;   // clear all; refresh covers state
            root.pendingUrls = {};
            noticeTimer.restart();
        }
        function onInstallFailed(url, reason) {
            root.notice = reason;
            root.pendingUrls = {};
            noticeTimer.restart();
        }
    }
    Timer { id: noticeTimer; interval: 6000; onTriggered: root.notice = "" }

    // The page's eased wheel-scroll. It was nested INSIDE the installed-row delegate
    // (the brace defect at the old :781-786), so it was instantiated once per row —
    // 4 competing NumberAnimations on page.contentY before stage 1a, 13 after. One
    // instance, at page level, is the whole point of the component.
    ScrollGlide { flick: page }

    // ---- which panes this world actually has ----------------------------------
    // Hemanth's ruling 2026-07-26: "remove discover and browser for the other worlds."
    // Both were Theatre surfaces wearing a world tab. Discover renders a hardcoded
    // curated rail list — Netflix Catalog and Marvel Universe, under the Tankoban tab —
    // and Browse queries a community registry that is entirely video add-ons, so a
    // world-filtered Browse would be permanently empty in Tankoban and Biblio. Rather
    // than invent per-world catalogue data to justify two panes, the other worlds have
    // the one pane that was ever true for them: what is installed.
    //
    // This also retires A5's P0-6 ("world tabs are decorative in two of three panes")
    // at the root instead of patching it: the decorative panes are gone.
    // Sources is world-agnostic — every world in one page, in rows — so the world tabs
    // are no longer page chrome. They belong to Installed, which is the one pane that
    // still filters by world, and Hemanth's ruling was that Browse and Installed "remain
    // just as they are". Showing the tabs anywhere else is what made them decorative in
    // two of three panes (A5's audit P0-6).
    readonly property bool worldTabsApply: pane === "installed"
    readonly property var paneModel: [
        { key: "sources",   label: "Sources" },
        { key: "browse",    label: "Browse everything" },
        { key: "installed", label: "Installed · " }
    ]

    // Browse loads when it first opens, and reloads on a sort or search change.
    onPaneChanged: if (pane === "browse" && !communityLoaded && !communityLoading) loadCommunity()
    onSortChanged: if (pane === "browse") loadCommunity()

    // A search filters whichever pane you are on. Sources and Installed match in place;
    // only Browse asks the registry, and only when you are already there. The earlier
    // jump-to-Browse behaviour existed because Discover could not answer a query at all —
    // Sources can, so the jump is gone rather than kept out of habit.
    Timer {
        id: queryDebounce
        interval: 450
        onTriggered: if (root.pane === "browse") root.loadCommunity()
    }
    onQueryChanged: queryDebounce.restart()

    MouseArea { anchors.fill: parent }
    Rectangle { anchors.fill: parent; color: "#000000" }

    // ---- live shell wallpaper (the 899a648 pattern) ----
    Item {
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Image { anchors.fill: parent; visible: root.backdrop === null
                source: "../assets/wallpaper/captured-motion.jpg"
                fillMode: Image.PreserveAspectCrop; cache: true }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.03, 0.04, 0.07, 0.86) }
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + 150
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }

        Column {
            id: col
            x: theme.margin
            width: root.width - theme.margin * 2
            topPadding: 14
            spacing: 0

            // ---- header ----
            Text { text: "COLOSSEUM · STORE"; color: theme.inkDimmer
                   font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.6; font.weight: Font.DemiBold }
            Text { text: "Extensions"; color: theme.ink; topPadding: 8
                   font.family: theme.display; font.pixelSize: 56; font.letterSpacing: -1 }
            Item { width: 1; height: 20 }
            Rectangle { width: 34; height: 3; radius: 2; color: theme.gold }

            Text {
                topPadding: 16
                textFormat: Text.StyledText
                font.family: theme.ui; font.pixelSize: 13
                color: theme.inkDimmer
                text: {
                    var n = root.installedList.length;
                    var on = 0;
                    for (var i = 0; i < root.installedList.length; i++)
                        if (root.installedList[i].enabled) on++;
                    // Real per-world counts. These were hardcoded as "Theatre = every
                    // install, Tankoban —, Biblio —", which was true only while the other
                    // two worlds were dead tabs. A world total can exceed the sum of the
                    // three, because one install may serve two worlds.
                    return "<b><font color='#f7f7f5'>" + n + "</font></b> installed"
                         + "  ·  " + (on === n ? "all carrying" : on + " carrying");
                }
            }

            // ---- worlds row: Installed's filter, not page chrome ----
            Row {
                visible: root.worldTabsApply
                height: visible ? implicitHeight : 0
                topPadding: root.worldTabsApply ? 34 : 0
                spacing: 34
                Repeater {
                    // All three worlds are live as of stage 1 — Tankoban and Biblio now
                    // carry real catalogues and wells, so the "arrives later" tag and both
                    // hand-written empty states are gone.
                    model: [
                        { key: "theatre", title: "Theatre", live: true },
                        { key: "tankoban", title: "Tankoban", live: true },
                        { key: "biblio", title: "Biblio", live: true }
                    ]
                    delegate: Item {
                        id: worldTab
                        required property var modelData
                        implicitWidth: worldRow.implicitWidth
                        implicitHeight: worldRow.implicitHeight + 12
                        Row {
                            id: worldRow
                            spacing: 7
                            Text {
                                text: worldTab.modelData.title
                                color: root.world === worldTab.modelData.key ? theme.ink
                                     : worldMa.containsMouse ? theme.inkDim : theme.inkDimmer
                                font.family: theme.display; font.pixelSize: 24
                            }
                            Text {
                                visible: !worldTab.modelData.live
                                anchors.top: parent.top
                                text: "arrives later"
                                color: theme.inkDimmer
                                font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 0.4
                            }
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: worldRow.implicitWidth; height: 3; radius: 2
                            color: theme.gold
                            visible: root.world === worldTab.modelData.key
                        }
                        MouseArea {
                            id: worldMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.world = worldTab.modelData.key
                        }
                    }
                }
            }

            // =================== THE LIVE STORE — all three worlds ===================
            // Was gated to Theatre only. Every world is live as of stage 1; the pane's
            // own rows filter by root.world.
            Column {
                width: col.width
                spacing: 0

                // ---- pane tabs + ONE global search + install-from-link ----
                Item {
                    width: col.width
                    height: 46
                    Row {
                        id: paneTabs
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 26
                        Repeater {
                            model: root.paneModel
                            delegate: Item {
                                id: paneTab
                                required property var modelData
                                implicitWidth: paneLabel.implicitWidth
                                implicitHeight: paneLabel.implicitHeight + 9
                                Text {
                                    id: paneLabel
                                    // Count what the pane will actually draw. Installed is
                                    // world-filtered, so it counts its world — but only while
                                    // a world is selectable; from Sources there is no world
                                    // in play and the honest number is the whole roster.
                                    text: paneTab.modelData.key === "installed"
                                          ? paneTab.modelData.label
                                            + (root.worldTabsApply ? root.countIn(root.world)
                                                                   : root.installedList.length)
                                          : paneTab.modelData.label
                                    color: root.pane === paneTab.modelData.key ? theme.ink
                                         : paneMa.containsMouse ? theme.inkDim : theme.inkDimmer
                                    font.family: theme.ui; font.pixelSize: 14
                                    font.weight: root.pane === paneTab.modelData.key ? Font.DemiBold : Font.Normal
                                }
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: paneLabel.implicitWidth; height: 2; radius: 1
                                    color: theme.gold
                                    visible: root.pane === paneTab.modelData.key
                                }
                                MouseArea {
                                    id: paneMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.pane = paneTab.modelData.key
                                }
                            }
                        }
                    }

                    // one search bar for the whole store — filters whichever room you're in
                    Rectangle {
                        id: searchBox
                        anchors.right: addLink.left
                        anchors.rightMargin: 24
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(280, col.width * 0.28)
                        height: 38
                        radius: 12
                        color: theme.glassTint
                        border.width: 1
                        border.color: searchInput.activeFocus ? Qt.rgba(0.94, 0.77, 0.29, 0.5) : theme.edge
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 13
                            anchors.rightMargin: 13
                            spacing: 9
                            Text { anchors.verticalCenter: parent.verticalCenter
                                   text: "⌕"; color: theme.inkDimmer; font.pixelSize: 15 }
                            TextInput {
                                id: searchInput
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 30
                                color: theme.ink
                                font.family: theme.ui; font.pixelSize: 14
                                clip: true
                                onTextChanged: root.query = text
                                Text {
                                    visible: !searchInput.text.length && !searchInput.activeFocus
                                    text: "Search extensions…"
                                    color: theme.inkDimmer
                                    font.family: theme.ui; font.pixelSize: 14
                                }
                            }
                        }
                    }
                    Text {
                        id: addLink
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Install from a link ›"
                        color: addMa.containsMouse ? "#ffd968" : theme.gold
                        font.family: theme.ui; font.pixelSize: 14
                        MouseArea {
                            id: addMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: sheet.openSheet()
                        }
                    }
                }

                Item { width: 1; height: 26 }

                // ============ DISCOVER ============
                // ── SOURCES — the world-agnostic pane (Hemanth 2026-07-26) ──────────
                // Every world in one page, in rows. Replaces Discover, whose curated rails
                // were a hardcoded Theatre list that appeared under every world tab.
                ExtensionsSources {
                    width: col.width
                    visible: root.pane === "sources"
                    installedList: root.installedList
                    query: root.query
                    onRemoveRequested: function (entry) { root.askRemove(entry) }
                    onConfigureRequested: function (entry) {
                        var url = root.configureUrl(entry.transportUrl);
                        if (url.length) Qt.openUrlExternally(url);
                    }
                }

                // ── the retired Discover rails, kept off the page ───────────────────
                Column {
                    width: col.width
                    visible: false
                    spacing: 0

                    // featured slab — steps aside while a search is on
                    Rectangle {
                        width: col.width
                        height: 156
                        visible: !root.query.length
                        radius: 20
                        border.width: 1; border.color: theme.edge
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: "#1a1c2c" }
                            GradientStop { position: 0.55; color: "#10111c" }
                            GradientStop { position: 1.0; color: "#0b0c14" }
                        }
                        Row {
                            anchors.fill: parent
                            anchors.margins: 30
                            spacing: 30
                            Item {
                                width: 96; height: 96
                                anchors.verticalCenter: parent.verticalCenter
                                AddonLogo {
                                    anchors.centerIn: parent
                                    addonId: Catalog.featured().id
                                    addonName: Catalog.featured().name
                                    size: 96; radius: 22
                                    tone1: "#2d2a1c"; tone2: "#181405"
                                }
                                Rectangle {   // gold ring — the featured accent
                                    anchors.fill: parent; color: "transparent"; radius: 22
                                    border.width: 1; border.color: Qt.rgba(0.94, 0.77, 0.29, 0.4)
                                }
                            }
                            Column {
                                width: parent.width - 96 - 180 - 60
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6
                                Text { text: "FEATURED EXTENSION"; color: theme.inkDimmer
                                       font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 2.2 }
                                Text { text: Catalog.featured().name; color: theme.ink
                                       font.family: theme.display; font.pixelSize: 32 }
                            }
                            // Derived, never asserted. This slab hardcoded "Installed" and
                            // "built-in": remove Torrentio and the largest element on the page
                            // went on claiming it was installed AND built in, while offering no
                            // control to install it back. Torrentio is core:false and removable,
                            // so the second line was false even before you touched anything.
                            // (A5's audit P0-5 — three of his four lenses reported it.)
                            //
                            // Same four-state verb the Discover cards carry, in TEXT not colour,
                            // and clickable in the one state where clicking means something.
                            Column {
                                id: featuredVerb
                                width: 180
                                anchors.verticalCenter: parent.verticalCenter
                                readonly property var item: Catalog.featured()
                                readonly property bool isCore: root.coreOf(featuredVerb.item)
                                readonly property bool isOn: root.carried(featuredVerb.item)
                                readonly property bool isPending:
                                    root.pendingUrls[featuredVerb.item.url] === true
                                // An explicit 44px-tall hit box, not a MouseArea hung off a
                                // Text's implicit size — the first version never received a
                                // click at all, proven by a probe that never fired.
                                Item {
                                    width: parent.width
                                    height: 44
                                    Text {
                                        id: featuredVerbLabel
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: featuredVerb.isCore ? "Built-in"
                                            : featuredVerb.isOn ? "Installed"
                                            : featuredVerb.isPending ? "Installing…"
                                            : "Install"
                                        color: featuredVerb.isCore || featuredVerb.isOn || featuredVerb.isPending
                                               ? theme.inkDimmer
                                               : featuredMa.containsMouse ? "#ffd968" : theme.gold
                                        font.family: theme.ui; font.pixelSize: 14
                                        font.weight: featuredVerb.isOn ? Font.Normal : Font.DemiBold
                                    }
                                    MouseArea {
                                        id: featuredMa
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Math.max(88, featuredVerbLabel.implicitWidth + 28)
                                        height: 44
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        enabled: !featuredVerb.isCore && !featuredVerb.isOn
                                                 && !featuredVerb.isPending
                                        onClicked: root.installFromCard(featuredVerb.item)
                                    }
                                }
                            }
                        }
                    }

                    // curated rails
                    Repeater {
                        model: Catalog.rails()
                        delegate: Column {
                            id: rail
                            required property var modelData
                            width: col.width
                            spacing: 0
                            visible: cardsRow.visibleCount > 0

                            Item { width: 1; height: 44 }
                            Row {
                                spacing: 14
                                Text { text: rail.modelData.title; color: theme.ink
                                       font.family: theme.display; font.pixelSize: 26 }
                                Text {
                                    anchors.baseline: parent.children[0].baseline
                                    text: rail.modelData.count
                                          + (rail.modelData.hint ? "  —  " + rail.modelData.hint : "")
                                    color: theme.inkDimmer
                                    font.family: theme.ui; font.pixelSize: 13
                                }
                            }
                            Item { width: 1; height: 16 }

                            Flickable {
                                width: col.width
                                height: 196
                                contentWidth: cardsRow.implicitWidth
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                flickableDirection: Flickable.HorizontalFlick
                                Row {
                                    id: cardsRow
                                    property int visibleCount: {
                                        var n = 0;
                                        for (var i = 0; i < rail.modelData.items.length; i++)
                                            if (root.hit(rail.modelData.items[i].name)) n++;
                                        return n;
                                    }
                                    spacing: 14
                                    Repeater {
                                        model: rail.modelData.items
                                        delegate: Rectangle {
                                            id: card
                                            required property var modelData
                                            visible: root.hit(card.modelData.name)
                                            width: 236; height: 132
                                            radius: 16
                                            color: cardMa.containsMouse ? Qt.rgba(0.06, 0.065, 0.09, 0.65)
                                                                        : Qt.rgba(0.04, 0.045, 0.065, 0.55)
                                            border.width: 1
                                            border.color: cardMa.containsMouse ? Qt.rgba(1, 1, 1, 0.28) : theme.edge

                                            Column {
                                                anchors.fill: parent
                                                anchors.margins: 18
                                                spacing: 0
                                                AddonLogo {
                                                    addonId: card.modelData.id
                                                    addonName: card.modelData.name
                                                    size: 42; radius: 11
                                                    tone1: card.modelData.tone1
                                                    tone2: card.modelData.tone2
                                                }
                                                Item { width: 1; height: 12 }
                                                Text { text: card.modelData.name; color: theme.ink
                                                       font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold }
                                            }
                                            MouseArea {
                                                id: cardMa
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                acceptedButtons: Qt.NoButton
                                            }
                                            // status tag — pinned to the tile's bottom-right corner
                                            Text {
                                                anchors.right: parent.right
                                                anchors.bottom: parent.bottom
                                                anchors.rightMargin: 18
                                                anchors.bottomMargin: 16
                                                text: card.modelData.core ? "Built-in"
                                                    : root.carried(card.modelData) ? "Installed"
                                                    : root.pendingUrls[card.modelData.url] ? "Installing…"
                                                    : "Install"
                                                color: card.modelData.core || root.carried(card.modelData)
                                                       || root.pendingUrls[card.modelData.url]
                                                       ? theme.inkDimmer
                                                       : verbMa.containsMouse ? "#ffd968" : theme.gold
                                                font.family: theme.ui; font.pixelSize: 13
                                                font.weight: root.carried(card.modelData) ? Font.Normal : Font.DemiBold
                                                MouseArea {
                                                    id: verbMa
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    enabled: !card.modelData.core && !root.carried(card.modelData)
                                                    onClicked: root.installFromCard(card.modelData)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ============ BROWSE ============
                Column {
                    width: col.width
                    visible: root.pane === "browse"
                    spacing: 0

                    Row {
                        spacing: 22
                        Repeater {
                            model: [
                                { key: "top", label: "Top" },
                                { key: "new", label: "New" },
                                { key: "rising", label: "Rising" }
                            ]
                            delegate: Item {
                                id: sortTab
                                required property var modelData
                                implicitWidth: sortLabel.implicitWidth
                                implicitHeight: sortLabel.implicitHeight + 7
                                Text {
                                    id: sortLabel
                                    text: sortTab.modelData.label
                                    color: root.sort === sortTab.modelData.key ? theme.ink
                                         : sortMa.containsMouse ? theme.inkDim : theme.inkDimmer
                                    font.family: theme.ui; font.pixelSize: 14
                                    font.weight: root.sort === sortTab.modelData.key ? Font.DemiBold : Font.Normal
                                }
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: sortLabel.implicitWidth; height: 2; radius: 1
                                    color: theme.gold
                                    visible: root.sort === sortTab.modelData.key
                                }
                                MouseArea {
                                    id: sortMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.sort = sortTab.modelData.key
                                }
                            }
                        }
                        Text {
                            leftPadding: 12
                            text: "a thousand community extensions · streams, catalogs, subtitles, live tv, tools"
                            color: theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 13
                        }
                    }

                    Item { width: 1; height: 18 }

                    Rectangle {
                        width: col.width
                        radius: 18
                        color: Qt.rgba(0.04, 0.045, 0.065, 0.48)
                        border.width: 1; border.color: theme.edge
                        height: communityCol.implicitHeight + 20

                        Column {
                            id: communityCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 10
                            anchors.leftMargin: 28
                            anchors.rightMargin: 28

                            Text {
                                visible: root.communityLoading
                                topPadding: 24; bottomPadding: 24
                                text: "Asking the registry…"
                                color: theme.inkDim
                                font.family: theme.display; font.italic: true; font.pixelSize: 16
                            }
                            Text {
                                visible: !root.communityLoading && root.communityRows.length === 0
                                topPadding: 24; bottomPadding: 24
                                text: root.query.length
                                      ? "Nothing in the registry matches “" + root.query + "”."
                                      : "The registry didn’t answer. Try again in a moment."
                                color: theme.inkDim
                                font.family: theme.display; font.italic: true; font.pixelSize: 16
                            }

                            Repeater {
                                model: root.communityRows
                                delegate: Item {
                                    id: crow
                                    required property var modelData
                                    required property int index
                                    width: communityCol.width
                                    height: 60
                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width; height: 1
                                        color: Qt.rgba(1, 1, 1, 0.06)
                                        visible: crow.index < root.communityRows.length - 1
                                    }
                                    Row {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width
                                        spacing: 18
                                        Text {
                                            width: 24
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: crow.index + 1
                                            color: theme.inkDimmer
                                            font.family: theme.display; font.pixelSize: 16
                                            horizontalAlignment: Text.AlignRight
                                        }
                                        AddonLogo {
                                            anchors.verticalCenter: parent.verticalCenter
                                            addonId: crow.modelData.id
                                            addonName: crow.modelData.name
                                            manifestLogo: crow.modelData.logo || ""
                                            size: 40; radius: 10
                                            tone1: crow.modelData.tone1
                                            tone2: crow.modelData.tone2
                                        }
                                        Column {
                                            width: parent.width - 24 - 40 - 120 - 110 - 18 * 4
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 3
                                            Text { text: crow.modelData.name; color: theme.ink
                                                   font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                                                   elide: Text.ElideRight; width: parent.width }
                                        }
                                        Text {
                                            width: 120
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: crow.modelData.stars > 0
                                            text: "★ " + crow.modelData.stars
                                            color: theme.inkDim
                                            font.family: theme.ui; font.pixelSize: 12
                                            horizontalAlignment: Text.AlignRight
                                        }
                                        Text {
                                            width: 110
                                            anchors.verticalCenter: parent.verticalCenter
                                            horizontalAlignment: Text.AlignRight
                                            text: root.carried(crow.modelData) ? "Installed"
                                                : root.pendingUrls[crow.modelData.url] ? "Installing…"
                                                : "Install"
                                            color: root.carried(crow.modelData) || root.pendingUrls[crow.modelData.url]
                                                   ? theme.inkDimmer
                                                   : crowVerbMa.containsMouse ? "#ffd968" : theme.gold
                                            font.family: theme.ui; font.pixelSize: 13
                                            font.weight: root.carried(crow.modelData) ? Font.Normal : Font.DemiBold
                                            MouseArea {
                                                id: crowVerbMa
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                enabled: !root.carried(crow.modelData)
                                                onClicked: root.installFromCard(crow.modelData)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        topPadding: 14
                        text: "Adult extensions are not carried in this store — by the house’s rule, not a toggle."
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 12
                    }
                }

                // ============ INSTALLED ============
                Column {
                    width: col.width
                    visible: root.pane === "installed"
                    spacing: 0

                    Rectangle {
                        width: col.width
                        radius: 18
                        color: Qt.rgba(0.04, 0.045, 0.065, 0.48)
                        border.width: 1; border.color: theme.edge
                        height: installedCol.implicitHeight + 20

                        Column {
                            id: installedCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 10
                            anchors.leftMargin: 28
                            anchors.rightMargin: 28

                            Repeater {
                                // World-filtered AND job-ordered, so the pane reads
                                // Catalogue-then-Wells without needing name suffixes to
                                // tell WeebCentral's two roles apart.
                                model: root.installedRowsFor(root.world)
                                delegate: Item {
                                    id: irow
                                    required property var modelData
                                    required property int index
                                    property var manifest: irow.modelData.manifest || ({})
                                    property bool isCore: irow.modelData.core === true
                                    property bool isOn: irow.modelData.enabled === true
                                    property var behaviorHints: irow.manifest.behaviorHints || ({})
                                    property bool configurable: irow.behaviorHints.configurable === true
                                    property bool configurationRequired:
                                        irow.behaviorHints.configurationRequired === true
                                    // Role (spec §3.1): a catalogue fills the shelves — locked,
                                    // unranked, never removable. A well fetches — ranked, removable.
                                    property bool isCatalogue: Catalog.isCatalogue(irow.modelData)
                                    // Rank is this world's filtered well index, so one shared row
                                    // reads 4 in Tankoban and 2 in Biblio with nothing stored.
                                    property int rank: irow.isCatalogue ? 0
                                                     : root.wellRank(irow.modelData, root.world)
                                    // An arrow is live only when it has somewhere to go IN THIS
                                    // WORLD. Asking the same resolver the click uses means the
                                    // control can never be offered for a move that won't happen —
                                    // the old arrows were always lit and silently did nothing.
                                    readonly property bool canMoveUp:
                                        !irow.isCatalogue && Catalog.moveDestination(
                                            root.installedList, root.world, irow.modelData.id, -1) !== null
                                    readonly property bool canMoveDown:
                                        !irow.isCatalogue && Catalog.moveDestination(
                                            root.installedList, root.world, irow.modelData.id, 1) !== null
                                    // A house well lives in-app and has no web page to open, so it
                                    // gets Settings; a remote addon keeps Configure ↗ (stage 4 builds
                                    // the sheet — until then only remote rows offer anything).
                                    property bool isHouse: String(irow.modelData.transportUrl || "")
                                                           .indexOf("colosseum://") === 0
                                    // The model is ordered Catalogue-then-Wells-then-rest, so a row
                                    // knows it opens a group when its job differs from the row above.
                                    // Drawing the header here keeps one Repeater and one delegate.
                                    readonly property string group: irow.isCatalogue ? "catalogue"
                                                                  : (Catalog.isWell(irow.modelData) ? "wells" : "rest")
                                    readonly property bool startsGroup: {
                                        var rows = root.installedRowsFor(root.world);
                                        if (irow.index <= 0) return true;
                                        var p = rows[irow.index - 1];
                                        if (!p) return true;
                                        var pg = Catalog.isCatalogue(p) ? "catalogue"
                                               : (Catalog.isWell(p) ? "wells" : "rest");
                                        return pg !== irow.group;
                                    }
                                    readonly property string groupTitle:
                                        irow.group === "catalogue" ? "Catalogue"
                                      : irow.group === "wells" ? "Wells" : "Also installed"
                                    // The model is already world-filtered and job-ordered;
                                    // only the search filter remains here.
                                    visible: root.hit(irow.manifest.name || irow.modelData.id)
                                    width: installedCol.width
                                    height: 82 + (irow.startsGroup ? 30 : 0)

                                    // ---- job header, drawn by the row that opens the group ----
                                    // The label alone. Its explanatory subtitle is gone by the
                                    // same ruling that took the row descriptions: the grouping
                                    // is legible without being narrated. (Hemanth, 2026-07-26.)
                                    Text {
                                        visible: irow.startsGroup
                                        anchors.top: parent.top
                                        anchors.topMargin: 12
                                        anchors.left: parent.left
                                        text: irow.groupTitle.toUpperCase()
                                        color: theme.gold
                                        font.family: theme.ui; font.pixelSize: 10
                                        font.letterSpacing: 2.4; font.bold: true
                                    }

                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width; height: 1
                                        color: Qt.rgba(1, 1, 1, 0.06)
                                        visible: irow.index < root.installedRowsFor(root.world).length - 1
                                    }
                                    MouseArea {
                                        id: irowMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.NoButton
                                    }

                                    Row {
                                        anchors.verticalCenter: parent.verticalCenter
                                        // sit below the job header when this row opens a group
                                        anchors.verticalCenterOffset: irow.startsGroup ? 15 : 0
                                        width: parent.width
                                        spacing: 18

                                        // rank + move up/down — the ask-order controls. Only wells
                                        // are ever ranked or reordered, which is what finally makes
                                        // this pane's own printed ordering law below true.
                                        Item {
                                            width: 18; height: 30
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: !irow.isCatalogue
                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                anchors.top: parent.top
                                                text: irow.rank
                                                color: theme.gold
                                                font.family: theme.ui; font.pixelSize: 12
                                                font.weight: Font.Bold
                                                opacity: irow.isOn ? 1 : 0.4
                                            }
                                        }
                                        Column {
                                            width: 18
                                            visible: !irow.isCatalogue
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 4
                                            opacity: irowMa.containsMouse ? 1 : 0.25
                                            Text {
                                                text: "▲"; font.pixelSize: 10
                                                color: upMa.containsMouse ? theme.ink : theme.inkDimmer
                                                opacity: irow.canMoveUp ? 1 : 0.3
                                                MouseArea { id: upMa; anchors.fill: parent; hoverEnabled: true
                                                            enabled: irow.canMoveUp
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: root.moveWell(irow.modelData.id, -1) }
                                            }
                                            Text {
                                                text: "▼"; font.pixelSize: 10
                                                color: downMa.containsMouse ? theme.ink : theme.inkDimmer
                                                opacity: irow.canMoveDown ? 1 : 0.3
                                                MouseArea { id: downMa; anchors.fill: parent; hoverEnabled: true
                                                            enabled: irow.canMoveDown
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: root.moveWell(irow.modelData.id, 1) }
                                            }
                                        }

                                        AddonLogo {
                                            anchors.verticalCenter: parent.verticalCenter
                                            opacity: irow.isOn ? 1 : 0.45
                                            addonId: irow.manifest.id || irow.modelData.id
                                            addonName: irow.manifest.name || irow.modelData.id
                                            manifestLogo: irow.manifest.logo || ""
                                            size: 44; radius: 11
                                        }

                                        Column {
                                            // Row omits invisible children from layout, so a
                                            // catalogue row (no rank, no grip) reclaims their
                                            // 18+18 widths and their two gaps.
                                            width: parent.width - 44 - 300 - 18 * 2
                                                   - (irow.isCatalogue ? 0 : 18 + 18 + 18 * 2)
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 4
                                            opacity: irow.isOn ? 1 : 0.45
                                            // The name carries the row. Hemanth's ruling 2026-07-26:
                                            // these need no explaining, and every line we wrote was
                                            // either obvious or wrong.
                                            Row {
                                                spacing: 10
                                                Text { text: irow.manifest.name || irow.modelData.id
                                                       color: theme.ink; font.family: theme.ui
                                                       font.pixelSize: 15; font.weight: Font.DemiBold }
                                                Text {
                                                    visible: irow.isCore
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: "THE HOUSE CATALOG"
                                                    color: theme.inkDimmer
                                                    font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 0.6
                                                }
                                            }
                                        }

                                        Row {
                                            width: 300
                                            anchors.verticalCenter: parent.verticalCenter
                                            layoutDirection: Qt.RightToLeft
                                            spacing: 22

                                            // the on/off switch: gold when carrying
                                            Rectangle {
                                                width: 40; height: 22; radius: 11
                                                anchors.verticalCenter: parent.verticalCenter
                                                color: irow.isOn ? Qt.rgba(0.94, 0.77, 0.29, 0.85)
                                                                 : Qt.rgba(1, 1, 1, 0.12)
                                                border.width: 1
                                                border.color: irow.isOn ? Qt.rgba(0.94, 0.77, 0.29, 0.5) : theme.edge
                                                opacity: irow.isCore ? 0.5 : 1
                                                Rectangle {
                                                    width: 16; height: 16; radius: 8
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    x: irow.isOn ? 20 : 2
                                                    color: irow.isOn ? "#141207" : theme.inkDim
                                                    Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                                                }
                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: irow.isCore ? Qt.ArrowCursor : Qt.PointingHandCursor
                                                    onClicked: if (!irow.isCore)
                                                                   Extensions.setEnabled(irow.modelData.id, !irow.isOn)
                                                }
                                            }
                                            Text {
                                                visible: !irow.isCore
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: "Remove"
                                                color: rmMa.containsMouse ? theme.ink : theme.inkDimmer
                                                font.family: theme.ui; font.pixelSize: 13
                                                MouseArea { id: rmMa; anchors.fill: parent; hoverEnabled: true
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: root.askRemove(irow.modelData) }
                                            }
                                            // A catalogue says plainly why there is no Remove, rather
                                            // than leaving an unexplained gap where every other row
                                            // has a verb.
                                            Text {
                                                visible: irow.isCore
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: "Locked"
                                                color: theme.inkDimmer
                                                opacity: 0.7
                                                font.family: theme.ui; font.pixelSize: 13
                                            }
                                            Text {
                                                visible: irow.configurable || irow.configurationRequired
                                                anchors.verticalCenter: parent.verticalCenter
                                                // A house well has no web page; Configure would silently
                                                // leave the app. The ↗ marks the outbound one, matching
                                                // the convention already at BiblioBook.qml:590.
                                                text: irow.configurationRequired
                                                      ? "Configure required"
                                                      : (irow.isHouse ? "Settings" : "Configure ↗")
                                                color: cfgMa.containsMouse ? theme.ink : theme.inkDim
                                                font.family: theme.ui; font.pixelSize: 13
                                                MouseArea {
                                                    id: cfgMa; anchors.fill: parent; hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: {
                                                        if (irow.isHouse) {
                                                            // Stage 4 builds the in-app sheet. Until then
                                                            // say so rather than opening a dead URL.
                                                            root.notice = (irow.manifest.name || "This well")
                                                                        + " settings arrive with the indexer sheet."
                                                            noticeTimer.restart()
                                                        } else {
                                                            var configure = root.configureUrl(
                                                                irow.modelData.transportUrl);
                                                            if (configure.length)
                                                                Qt.openUrlExternally(configure);
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

                    Text {
                        topPadding: 14
                        text: "Order matters: when you press play, sources are asked in this order, top first."
                        color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 12
                    }
                }
            }

        }
    }

    // ---- top chrome: back · minimize · power (fullscreen-only vocabulary) ----
    Item {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 24
        anchors.rightMargin: theme.margin
        width: chromeRow.implicitWidth
        height: 30
        Row {
            id: chromeRow
            spacing: 22
            Text { text: "⌕"; color: sMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: sMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.searchClicked() } }
            Text { text: "—"; color: mMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: mMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() } }
            Text { text: "⛶"; color: fMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: fMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.fullscreenRequested() } }
            Text { text: "⏻"; color: pMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: pMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() } }
        }
    }
    BackAction {
        variant: "capsule"; tip: "Back"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: 21
        anchors.leftMargin: theme.margin - 10
        onTriggered: root.backRequested()
    }

    // ---- one quiet notice line (install results) ----
    Rectangle {
        visible: root.notice.length > 0
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 92
        width: noticeT.implicitWidth + 44
        height: 42
        radius: 12
        color: Qt.rgba(0.05, 0.055, 0.08, 0.92)
        border.width: 1; border.color: theme.edge
        Text { id: noticeT; anchors.centerIn: parent
               text: root.notice; color: theme.inkDim
               font.family: theme.ui; font.pixelSize: 13 }
    }

    // ---- install-from-link sheet ----
    Rectangle {
        id: sheet
        anchors.fill: parent
        color: Qt.rgba(0.015, 0.02, 0.035, 0.62)
        visible: false

        property string previewUrl: ""
        property var previewManifest: null
        property string error: ""
        property bool checking: false

        function openSheet() {
            previewUrl = ""; previewManifest = null; error = ""; checking = false;
            urlInput.text = "";
            visible = true;
            urlInput.forceActiveFocus();
        }
        function openForUrl(rawUrl) {
            openSheet();
            urlInput.text = String(rawUrl || "");
            check();
        }
        function closeSheet() { visible = false }
        function check() {
            if (!urlInput.text.trim().length) return;
            error = ""; previewManifest = null; checking = true;
            previewUrl = Extensions.normalizeUrl(urlInput.text);
            Extensions.preview(urlInput.text);
        }

        Connections {
            target: typeof Extensions !== "undefined" ? Extensions : null
            function onPreviewReady(url, manifest) {
                if (!sheet.visible || url !== sheet.previewUrl) return;
                sheet.previewManifest = manifest;
                sheet.checking = false;
            }
            function onPreviewFailed(url, reason) {
                if (!sheet.visible || url !== sheet.previewUrl) return;
                sheet.error = reason;
                sheet.checking = false;
            }
            function onInstallFinished(id, name) {
                if (sheet.visible) sheet.closeSheet();
            }
        }

        MouseArea { anchors.fill: parent; onClicked: sheet.closeSheet() }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(560, root.width * 0.9)
            height: sheetCol.implicitHeight + 64
            radius: 20
            color: Qt.rgba(0.05, 0.055, 0.08, 0.94)
            border.width: 1; border.color: theme.edge
            MouseArea { anchors.fill: parent }   // swallow the dismiss click

            Column {
                id: sheetCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 32
                spacing: 0

                Text { text: "Install from a link"; color: theme.ink
                       font.family: theme.display; font.pixelSize: 26 }
                Text {
                    topPadding: 8
                    width: parent.width
                    text: "Paste an extension’s address. The house reads what it offers and shows you before anything is added."
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
                Text {
                    topPadding: 6
                    width: parent.width
                    text: "For configurable sources, paste the final configured manifest URL after using Configure."
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                Item { width: 1; height: 20 }
                Rectangle {
                    width: parent.width
                    height: 44
                    radius: 12
                    color: theme.glassTint
                    border.width: 1
                    border.color: urlInput.activeFocus ? Qt.rgba(0.94, 0.77, 0.29, 0.5) : theme.edge
                    TextInput {
                        id: urlInput
                        anchors.fill: parent
                        anchors.leftMargin: 15
                        anchors.rightMargin: 15
                        verticalAlignment: TextInput.AlignVCenter
                        color: theme.ink
                        font.family: theme.ui; font.pixelSize: 14
                        clip: true
                        onAccepted: sheet.check()
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !urlInput.text.length && !urlInput.activeFocus
                            text: "https://…/manifest.json   or   stremio://…"
                            color: theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 13
                        }
                    }
                }

                Text {
                    visible: sheet.checking
                    topPadding: 16
                    text: "Reading the manifest…"
                    color: theme.inkDim
                    font.family: theme.display; font.italic: true; font.pixelSize: 15
                }
                Text {
                    visible: sheet.error.length > 0
                    topPadding: 16
                    width: parent.width
                    text: sheet.error
                    color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    visible: sheet.previewManifest !== null
                    width: parent.width
                    height: 74
                    radius: 14
                    color: Qt.rgba(0.94, 0.77, 0.29, 0.06)
                    border.width: 1; border.color: Qt.rgba(0.94, 0.77, 0.29, 0.35)
                    Row {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 15
                        AddonLogo {
                            anchors.verticalCenter: parent.verticalCenter
                            addonId: sheet.previewManifest ? (sheet.previewManifest.id || "") : ""
                            addonName: sheet.previewManifest ? (sheet.previewManifest.name || "") : ""
                            manifestLogo: sheet.previewManifest ? (sheet.previewManifest.logo || "") : ""
                            size: 44; radius: 11
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 60
                            spacing: 3
                            Text { text: sheet.previewManifest ? sheet.previewManifest.name : ""
                                   color: theme.ink; font.family: theme.ui
                                   font.pixelSize: 14; font.weight: Font.DemiBold }
                            Text {
                                width: parent.width
                                text: {
                                    if (!sheet.previewManifest) return "";
                                    var m = sheet.previewManifest;
                                    var res = m.resources || [];
                                    var names = [];
                                    for (var i = 0; i < res.length; i++)
                                        names.push(typeof res[i] === "string" ? res[i] : res[i].name);
                                    var host = sheet.previewUrl.replace(/^https?:\/\//, "").split("/")[0];
                                    return "gives " + (names.join(", ") || "resources") + " · from " + host;
                                }
                                color: theme.inkDimmer
                                font.family: theme.ui; font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                Item { width: 1; height: 24 }
                Row {
                    anchors.right: parent.right
                    spacing: 26
                    Text {
                        text: "Cancel"
                        color: cancelMa.containsMouse ? theme.ink : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 14
                        MouseArea { id: cancelMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor; onClicked: sheet.closeSheet() }
                    }
                    Text {
                        text: sheet.previewManifest
                              ? ((sheet.previewManifest.behaviorHints || {}).configurationRequired
                                 ? "Configure ↗"
                                 : "Install " + sheet.previewManifest.name)
                              : "Read it first"
                        color: readMa.containsMouse ? "#ffd968" : theme.gold
                        font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                        MouseArea {
                            id: readMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (!sheet.previewManifest) {
                                    sheet.check();
                                    return;
                                }
                                if ((sheet.previewManifest.behaviorHints || {}).configurationRequired) {
                                    var configure = root.configureUrl(sheet.previewUrl);
                                    if (configure.length) Qt.openUrlExternally(configure);
                                } else {
                                    Extensions.install(sheet.previewUrl);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
