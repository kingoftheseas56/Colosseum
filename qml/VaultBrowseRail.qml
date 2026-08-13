// VaultBrowseRail — the Vault Browse face's collapsible root rail (locked design decision #10,
// execution plan Slice 5). Collapsed by default: each confirmed/synthetic root renders as a
// glyph with its availability dot; expanding adds names and counts only — it never reveals
// state that was hidden while collapsed (design §4.1: "Expanding never reveals state that was
// hidden — only detail"). Also carries the two capability affordances the old marquee/tab-bar
// owned: Add storage (→ existing addFolder) and the reversible Hidden shelf.
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

    signal rootSelected(string path)
    signal hiddenRequested()
    signal addRequested()
    signal toggleRequested()

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

        // ---- one row per confirmed/synthetic root ----
        Repeater {
            model: rail.roots
            delegate: Item {
                id: rootRow
                required property var modelData
                required property int index
                objectName: "vaultBrowseRailRoot_" + rootRow.index
                property bool available: !!rootRow.modelData.available
                property string rootPath: rootRow.modelData.path || ""
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
                    anchors.right: parent.right; anchors.rightMargin: rail.expanded ? 12 : 0
                    anchors.verticalCenter: parent.verticalCenter
                    height: 26

                    Item {
                        id: rootIcon
                        width: 26; height: 26
                        anchors.left: parent.left
                        anchors.horizontalCenter: rail.expanded ? undefined : parent.horizontalCenter
                        Image {
                            anchors.centerIn: parent
                            width: 18; height: 18
                            opacity: rootRow.available ? 0.85 : 0.4
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
                            width: parent.width
                            elide: Text.ElideRight
                            text: rootRow.modelData.name || ""
                            color: rootRow.available ? theme.ink : theme.inkDimmer
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
    }
}
