// VaultBrowseRail — the Vault Browse face's collapsible root rail (locked design decision #10,
// execution plan Slice 5). Collapsed by default: each confirmed/synthetic root renders as a
// glyph with its availability dot; expanding adds names and counts only — it never reveals
// state that was hidden while collapsed (design §4.1: "Expanding never reveals state that was
// hidden — only detail"). Also carries the capability affordances the old marquee/tab-bar
// owned: Add storage (→ existing addFolder) and the reversible Hidden shelf, plus (vault ux
// uplift S9) the marquee "· N folders" count in the header and the synthetic downloads root's
// quiet, always-last chip treatment with its remove action, and (S10) each row's overflow
// menu — Rescan · "Forget this storage…" (confirm copy states files on disk are untouched) ·
// the root's path — with the scan-ignore needle editor reachable from the footer.
import QtQuick

Item {
    id: rail
    objectName: "vaultBrowseRail"
    // Slice 9 (keyboard reach, design §4.9): "the rail is reachable by Tab." A plain Item needs
    // this explicitly — Qt Quick's automatic Tab-focus chain otherwise skips it, and
    // VaultPage.qml wires the explicit KeyNavigation.tab/.backtab pairing with the grid.
    activeFocusOnTab: true

    property var roots: []                 // VaultLibrary.rootsDetail() rows: {path,name,available,itemCount,fileCount}
    property bool expanded: false
    property string selectedRootPath: ""
    property bool hiddenActive: false
    property int hiddenCount: 0
    // S9 (vault ux uplift) — the synthetic downloads root's normalized path ("" when no
    // synthetic root is wired). The chip that matches it gets the spec's "visually quiet,
    // always last" treatment: muted icon + name, sorted last regardless of the order
    // rootsDetail() happened to return, plus its remove affordance.
    property string downloadsRootPath: ""
    // S9 — the marquee "· N folders" count. Fed from VaultLibrary.rootCount() (NOT
    // roots.length) so the marquee states the same truth the chip count derives from.
    property int rootFolderCount: 0
    // S10 (vault ux uplift) — the current scanIgnore needles (joined into the editor's
    // field when it opens). VaultPage re-reads them from VaultLibrary.scanIgnore() on the
    // revision clock; setScanIgnore's republish bumps that clock.
    property var scanIgnore: []

    signal rootSelected(string path)
    signal hiddenRequested()
    signal addRequested()
    signal toggleRequested()
    // S9 — the downloads chip's remove action (VaultLibrary.removeDownloadsRoot(): full
    // hide + republish; the files + transfer history on the Downloads lane are untouched).
    signal removeDownloadsRequested()
    // S10 — the row overflow menu's verbs. VaultPage wires them to the façade:
    // rescanRoot(path) / forgetRoot(path); scanIgnoreSaved carries the editor's parsed
    // needle list to setScanIgnore().
    signal rescanRequested(string path)
    signal forgetConfirmed(string path)
    signal scanIgnoreSaved(var needles)

    // S10 — the row overflow menu's open state. `menuRow` holds the row whose menu is up
    // (null = closed); `menuRowY` is that row's y mapped into rail coordinates at open
    // time (imperative mapping — no duplicated geometry math). Any roots repaint closes
    // the menu (a publish can invalidate the row).
    property var menuRow: null
    property real menuRowY: 0
    function openRowMenu(row) {
        rail.forgetArmed = false
        rail.menuRow = row
        rail.menuRowY = row.mapToItem(rail, 0, 0).y
    }
    function closeRowMenu() {
        rail.menuRow = null
        rail.forgetArmed = false
    }

    // S9 — the display order: user roots in rootsDetail() order, the synthetic downloads
    // root ALWAYS last (design decision 4: "the downloads root last and visually quiet …
    // and never leads the view"). A computed copy, never a mutation of `roots`.
    readonly property var displayRoots: {
        const dl = rail.downloadsRootPath
        if (!dl) return rail.roots
        const users = [], downloads = []
        for (let i = 0; i < rail.roots.length; ++i)
            (rail.roots[i].path === dl ? downloads : users).push(rail.roots[i])
        return downloads.length ? users.concat(downloads) : users
    }
    // A publish can invalidate the open row — any repaint of the display model closes the
    // menu (menus are transient by design).
    onDisplayRootsChanged: rail.closeRowMenu()

    readonly property int collapsedWidth: 62
    readonly property int expandedWidth: 236
    width: rail.expanded ? rail.expandedWidth : rail.collapsedWidth
    Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
    // A plain Item has no implicit size of its own — VaultPage.qml always anchors both top AND
    // bottom (an explicit height that wins over this), but implicitHeight makes the component
    // self-sufficient for any other consumer (e.g. a Quick Test harness) that only sets `y`.
    implicitHeight: col.implicitHeight
    clip: true

    Theme { id: theme }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(1, 1, 1, 0.02)
        radius: 14
        border.width: 1
        border.color: theme.edge
    }

    Column {
        id: col
        anchors.fill: parent
        anchors.margins: rail.expanded ? 12 : 9
        spacing: 2

        // ---- header: "Storage" label (expanded only) + the toggle ----
        Item {
            width: col.width
            height: 26
            Text {
                visible: rail.expanded
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                text: "STORAGE"
                color: theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 1.5; font.weight: Font.DemiBold
            }
            // S9 — the marquee "· N folders" line (VaultLibrary.rootCount() through
            // rootFolderCount): the roots strip's one honest count, singular/plural correct,
            // hidden when there is nothing to count.
            Text {
                objectName: "vaultBrowseRailFolderCount"
                visible: rail.expanded && rail.rootFolderCount > 0
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: toggleBtn.left; anchors.rightMargin: 8
                text: "· " + rail.rootFolderCount + (rail.rootFolderCount === 1 ? " folder" : " folders")
                color: theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 11
            }
            Rectangle {
                id: toggleBtn
                objectName: "vaultBrowseRailToggle"
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: rail.expanded ? parent.right : undefined
                anchors.horizontalCenter: rail.expanded ? undefined : parent.horizontalCenter
                width: 26; height: 26; radius: 7
                color: toggleMa.containsMouse ? theme.glassHi : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: rail.expanded ? "‹" : "›"   // ‹ / › — collapse/expand chevron
                    color: theme.inkDimmer
                    font.pixelSize: 15
                }
                MouseArea {
                    id: toggleMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: rail.toggleRequested()
                }
            }
        }

        // ---- one row per confirmed/synthetic root (S9: downloads root always LAST, muted) ----
        Repeater {
            model: rail.displayRoots
            delegate: Item {
                id: rootRow
                required property var modelData
                required property int index
                objectName: "vaultBrowseRailRoot_" + rootRow.index
                property bool available: !!rootRow.modelData.available
                property string rootPath: rootRow.modelData.path || ""
                // S9 — the synthetic downloads root renders quiet: muted glyph + dim name
                // (spec decision 4: it never leads the view). Exposed as `muted` so the
                // harness can pin the treatment without scraping colors.
                readonly property bool isDownloads: rootRow.rootPath !== ""
                                                   && rootRow.rootPath === rail.downloadsRootPath
                readonly property bool muted: rootRow.isDownloads
                width: col.width
                height: 40

                Rectangle {
                    anchors.fill: parent
                    radius: 10
                    color: rootRow.rootPath === rail.selectedRootPath && !rail.hiddenActive
                           ? theme.glassHi : (rowMa.containsMouse ? theme.glassTint : "transparent")
                }

                // A plain Item, not a Row: Row forbids left/right/horizontalCenter/fill/centerIn
                // anchors on its own children, and the icon needs to re-center itself when the
                // rail collapses — so this positions the icon + detail column by hand instead.
                Item {
                    id: rootRowContent
                    anchors.left: parent.left; anchors.leftMargin: rail.expanded ? 12 : 0
                    anchors.right: parent.right
                    anchors.rightMargin: rail.expanded ? (rootRow.isDownloads ? 54 : 26) : 0
                    anchors.verticalCenter: parent.verticalCenter
                    height: 26

                    Item {
                        id: rootIcon
                        width: 26; height: 26
                        anchors.left: parent.left
                        anchors.horizontalCenter: rail.expanded ? undefined : parent.horizontalCenter
                        Image {
                            objectName: "vaultBrowseRailRootGlyph"
                            anchors.centerIn: parent
                            width: 18; height: 18
                            // S9: a muted row's glyph sits at the away-glyph dimness —
                            // present, never demanding.
                            opacity: rootRow.muted ? 0.4 : (rootRow.available ? 0.85 : 0.4)
                            source: "../assets/icons/vault-folder.svg"
                            fillMode: Image.PreserveAspectFit
                        }
                        Rectangle {
                            // availability dot — filled when available, ringed-only when away
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.rightMargin: -1; anchors.bottomMargin: -1
                            width: 8; height: 8; radius: 4
                            color: rootRow.available ? theme.inkDim : "transparent"
                            border.width: rootRow.available ? 0 : 1.4
                            border.color: theme.inkDimmer
                            Rectangle { anchors.fill: parent; radius: 4; color: "transparent"
                                        border.width: 1.5; border.color: Qt.rgba(0.08, 0.08, 0.10, 1) }
                        }
                    }

                    Column {
                        visible: rail.expanded
                        anchors.left: rootIcon.right; anchors.leftMargin: 11
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Text {
                            objectName: "vaultBrowseRailRootName"
                            width: parent.width
                            elide: Text.ElideRight
                            text: rootRow.modelData.name || ""
                            // S9: the muted row's name sits at the away-name dimness.
                            color: rootRow.muted ? theme.inkDimmer
                                                 : (rootRow.available ? theme.ink : theme.inkDimmer)
                            font.family: theme.ui; font.pixelSize: 13
                        }
                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            text: (rootRow.modelData.itemCount || 0) + " items · "
                                  + (rootRow.modelData.fileCount || 0) + " files"
                            color: theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 12
                        }
                    }
                }

                MouseArea {
                    id: rowMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: rail.rootSelected(rootRow.rootPath)
                }

                // S10 — the row overflow menu's handle (⋮), above the row hit area like
                // the remove ×. Sits left of the × on the downloads row so the two never
                // overlap; opens the shared menu anchored at this row.
                Item {
                    id: rowOverflow
                    objectName: "vaultBrowseRailRowOverflow"
                    visible: rail.expanded
                             && (rowMa.containsMouse || overflowMa.containsMouse
                                 || rail.menuRow === rootRow)
                    anchors.right: parent.right
                    anchors.rightMargin: rootRow.isDownloads ? 28 : 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 22; height: 22
                    Text {
                        anchors.centerIn: parent
                        text: "⋮"
                        color: overflowMa.containsMouse ? theme.ink : theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 14
                    }
                    MouseArea {
                        id: overflowMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: rail.openRowMenu(rootRow)
                    }
                }

                // S9 — the downloads chip's remove action. Quiet: a small × that only exists
                // on the muted row, only while expanded, only on hover. Declared AFTER rowMa
                // so it stacks above the row's own hit area (a click on × removes, never
                // selects). VaultPage wires the signal to VaultLibrary.removeDownloadsRoot()
                // (hide + republish — the files on the Downloads lane are never touched).
                Item {
                    id: downloadsRemove
                    objectName: "vaultBrowseRailDownloadsRemove"
                    visible: rail.expanded && rootRow.isDownloads
                             && (rowMa.containsMouse || removeMa.containsMouse)
                    anchors.right: parent.right; anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 22; height: 22
                    Text {
                        anchors.centerIn: parent
                        text: "×"
                        color: removeMa.containsMouse ? theme.ink : theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 14
                    }
                    MouseArea {
                        id: removeMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: rail.removeDownloadsRequested()
                    }
                }
            }
        }

        // ---- the reversible Hidden shelf (design §0 acceptance: hidden shelf reachable) ----
        Item {
            id: hiddenRow
            objectName: "vaultBrowseRailHidden"
            width: col.width
            height: 40

            Rectangle {
                anchors.fill: parent
                radius: 10
                color: rail.hiddenActive ? theme.glassHi : (hiddenMa.containsMouse ? theme.glassTint : "transparent")
            }
            Item {
                id: hiddenRowContent
                anchors.left: parent.left; anchors.leftMargin: rail.expanded ? 12 : 0
                anchors.right: parent.right; anchors.rightMargin: rail.expanded ? 12 : 0
                anchors.verticalCenter: parent.verticalCenter
                height: 26
                Item {
                    id: hiddenIcon
                    width: 26; height: 26
                    anchors.left: parent.left
                    anchors.horizontalCenter: rail.expanded ? undefined : parent.horizontalCenter
                    // hand-drawn eye-slash — a slashed ellipse, the same two-primitive technique
                    // the card's away glyph already uses (no icon asset invented for this).
                    Item {
                        anchors.centerIn: parent
                        width: 16; height: 16
                        Rectangle {
                            anchors.centerIn: parent
                            width: parent.width; height: parent.width * 0.62
                            radius: height / 2
                            color: "transparent"
                            border.width: 1.4; border.color: theme.inkDim
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            width: parent.width * 1.2; height: 1.4
                            rotation: 45
                            color: theme.inkDim
                        }
                    }
                }
                Text {
                    visible: rail.expanded
                    anchors.left: hiddenIcon.right; anchors.leftMargin: 11
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Hidden" + (rail.hiddenCount > 0 ? " · " + rail.hiddenCount : "")
                    color: theme.ink
                    font.family: theme.ui; font.pixelSize: 13
                }
            }
            MouseArea {
                id: hiddenMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: rail.hiddenRequested()
            }
        }

        Item { width: 1; height: 6 }

        // ---- Add storage (→ the existing addFolder ceremony) ----
        Item {
            id: addRow
            objectName: "vaultBrowseRailAdd"
            width: col.width
            height: 40

            Rectangle {
                anchors.fill: parent
                radius: 10
                color: "transparent"
                border.width: 1
                border.color: theme.edge
            }
            Item {
                id: addRowContent
                anchors.left: parent.left; anchors.leftMargin: rail.expanded ? 12 : 0
                anchors.right: parent.right; anchors.rightMargin: rail.expanded ? 12 : 0
                anchors.verticalCenter: parent.verticalCenter
                height: 26
                Item {
                    id: addIcon
                    width: 26; height: 26
                    anchors.left: parent.left
                    anchors.horizontalCenter: rail.expanded ? undefined : parent.horizontalCenter
                    Rectangle { anchors.centerIn: parent; width: 12; height: 1.6; color: theme.inkDimmer }
                    Rectangle { anchors.centerIn: parent; width: 1.6; height: 12; color: theme.inkDimmer }
                }
                Text {
                    visible: rail.expanded
                    anchors.left: addIcon.right; anchors.leftMargin: 11
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Add storage"
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 13
                }
            }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: rail.addRequested()
            }
        }

        Item { width: 1; height: 4 }

        // ---- S10 — Ignore patterns (→ the modest needle editor, reachable from the footer) ----
        Item {
            id: ignoreRow
            objectName: "vaultBrowseRailIgnore"
            visible: rail.expanded
            width: col.width
            height: 26
            Text {
                anchors.left: parent.left; anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: "Ignore patterns"
                color: ignoreMa.containsMouse ? theme.ink : theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 12
            }
            MouseArea {
                id: ignoreMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    ignoreField.text = (rail.scanIgnore || []).join(", ")
                    ignoreEditor.open()
                }
            }
        }
    }

    // ── S10 — the shared row overflow menu (Rescan · Forget this storage… · the path),
    //        hand-rolled like every other house popup: a click-away backing over the rail
    //        plus one glass panel anchored under the open row. Both panes live INSIDE the
    //        rail (clip is fine — the expanded rail is wide enough for the panel and tall
    //        enough for either state), so no parentage tricks and no Quick Controls. ──
    Item {
        id: rowMenu
        objectName: "vaultBrowseRailRowMenu"
        visible: rail.menuRow !== null
        anchors.fill: parent
        z: 50

        // click-away close: swallows every click that is not on the panel itself
        MouseArea { anchors.fill: parent; onClicked: rail.closeRowMenu() }

        Rectangle {
            id: rowMenuPanel
            x: 3
            width: rail.width - 6
            height: menuColumn.visible ? menuColumn.implicitHeight + 16
                                       : confirmColumn.implicitHeight + 16
            y: Math.min(rail.menuRowY + 42, rail.height - height - 6)
            radius: 12
            color: Qt.rgba(0.055, 0.06, 0.09, 0.98)
            border.width: 1
            border.color: theme.edge

            // state 1 — the actions
            Column {
                id: menuColumn
                visible: !forgetArmed
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2
                Item {
                    objectName: "vaultBrowseRailMenuRescan"
                    width: parent.width; height: 34
                    Text {
                        anchors.left: parent.left; anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Rescan"
                        color: rescanMa.containsMouse ? theme.ink : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 13
                    }
                    MouseArea {
                        id: rescanMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            rail.rescanRequested(rail.menuRow ? rail.menuRow.rootPath : "")
                            rail.closeRowMenu()
                        }
                    }
                }
                Rectangle { width: parent.width; height: 1; color: theme.edge; opacity: 0.6 }
                Item {
                    objectName: "vaultBrowseRailMenuForget"
                    width: parent.width; height: 34
                    Text {
                        anchors.left: parent.left; anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Forget this storage…"
                        color: forgetMa.containsMouse ? theme.ink : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 13
                    }
                    MouseArea {
                        id: forgetMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: forgetArmed = true
                    }
                }
                Rectangle { width: parent.width; height: 1; color: theme.edge; opacity: 0.6 }
                // the root's own path — the menu's one honest fact line, never clickable
                Text {
                    objectName: "vaultBrowseRailMenuPath"
                    width: parent.width
                    elide: Text.ElideMiddle
                    text: rail.menuRow ? rail.menuRow.rootPath : ""
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 11
                    topPadding: 8; bottomPadding: 6; leftPadding: 10
                }
            }

            // state 2 — the forget confirmation. The copy MUST state that files on disk
            // are untouched (vault ux uplift S10 spec) — forgetting is a Colosseum-side
            // removal, never a deletion.
            Column {
                id: confirmColumn
                visible: forgetArmed
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8
                Text {
                    objectName: "vaultBrowseRailMenuCopy"
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "Forget this storage? It is removed from Colosseum only — "
                          + "your files on disk are untouched."
                    color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: 12; lineHeight: 1.25
                }
                Row {
                    spacing: 8
                    Item {
                        height: 30; width: 92
                        Rectangle { anchors.fill: parent; radius: 8; color: Qt.rgba(0.94, 0.77, 0.29, 0.9) }
                        Text {
                            anchors.centerIn: parent
                            text: "Forget"
                            color: "#141207"
                            font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                        }
                        MouseArea {
                            objectName: "vaultBrowseRailMenuConfirm"
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                rail.forgetConfirmed(rail.menuRow ? rail.menuRow.rootPath : "")
                                rail.closeRowMenu()
                            }
                        }
                    }
                    Item {
                        height: 30; width: 78
                        Rectangle { anchors.fill: parent; radius: 8; color: "transparent"
                                    border.width: 1; border.color: theme.edge }
                        Text {
                            anchors.centerIn: parent
                            text: "Cancel"
                            color: theme.inkDim
                            font.family: theme.ui; font.pixelSize: 12
                        }
                        MouseArea {
                            objectName: "vaultBrowseRailMenuCancel"
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            // Cancel disarms the confirm but keeps the menu up (the user
                            // may still want Rescan); click-away closes the whole menu.
                            onClicked: rail.forgetArmed = false
                        }
                    }
                }
            }
        }
    }

    // S10 — the menu's confirm state (menu open → "Forget this storage…" arms it).
    property bool forgetArmed: false

    // ── S10 — the modest scan-ignore needle editor, opened from the rail footer. One
    //        line, comma/semicolon separated; Save parses → scanIgnoreSaved(needles). ──
    Item {
        id: ignoreEditor
        objectName: "vaultBrowseRailIgnoreEditor"
        visible: false
        anchors.fill: parent
        z: 60

        function open() { rail.forgetArmed = false; rail.closeRowMenu(); ignoreEditor.visible = true }
        function close() { ignoreEditor.visible = false }

        MouseArea { anchors.fill: parent; onClicked: ignoreEditor.close() }

        Rectangle {
            x: 3
            width: rail.width - 6
            height: 168
            y: rail.height - height - 8
            radius: 12
            color: Qt.rgba(0.055, 0.06, 0.09, 0.98)
            border.width: 1
            border.color: theme.edge

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8
                Text {
                    text: "IGNORE PATTERNS"
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 1.4
                                              font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "Folders whose full path contains any of these words are skipped while scanning."
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 11; lineHeight: 1.25
                }
                Rectangle {
                    width: parent.width
                    height: 34
                    radius: 8
                    color: Qt.rgba(1, 1, 1, 0.04)
                    border.width: 1
                    border.color: ignoreField.activeFocus ? theme.inkDimmer : theme.edge
                    TextInput {
                        id: ignoreField
                        objectName: "vaultBrowseRailIgnoreField"
                        anchors.fill: parent
                        anchors.margins: 8
                        text: ""
                        color: theme.ink
                        selectionColor: theme.gold
                        selectedTextColor: "#141207"
                        font.family: theme.ui; font.pixelSize: 12
                        verticalAlignment: TextInput.AlignVCenter
                        clip: true
                    }
                }
                Row {
                    spacing: 8
                    Item {
                        height: 28; width: 62
                        Rectangle { anchors.fill: parent; radius: 8; color: Qt.rgba(0.94, 0.77, 0.29, 0.9) }
                        Text {
                            anchors.centerIn: parent
                            text: "Save"
                            color: "#141207"
                            font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                        }
                        MouseArea {
                            objectName: "vaultBrowseRailIgnoreSave"
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                const needles = ignoreField.text.split(/[,;]/)
                                      .map(s => s.trim()).filter(s => s.length > 0)
                                rail.scanIgnoreSaved(needles)
                                ignoreEditor.close()
                            }
                        }
                    }
                    Item {
                        height: 28; width: 78
                        Rectangle { anchors.fill: parent; radius: 8; color: "transparent"
                                    border.width: 1; border.color: theme.edge }
                        Text {
                            anchors.centerIn: parent
                            text: "Cancel"
                            color: theme.inkDim
                            font.family: theme.ui; font.pixelSize: 12
                        }
                        MouseArea {
                            objectName: "vaultBrowseRailIgnoreCancel"
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ignoreEditor.close()
                        }
                    }
                }
            }
        }
    }
}
