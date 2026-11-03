// DlnaSharePopup — "Share on your Wi-Fi": the top-bar access-point glyph's flyout.
// Plain full-window layer with a click-catcher (same shape as AccountFlyout: modal Popup
// overlays fight Qt's grab semantics on this shell). Text-led glass card; the glyph is
// decoration only. Everything is driven by the `dlnaSharing` context property and tolerates it
// being absent (feature compiled out), so the app runs before/without the native side.
//
// Contract read from dlnaSharing: enabled, running, busy, friendlyName, itemCount,
// shareTypes (QStringList), networkLabel, error; invokable setEnabled(bool).
import QtQuick
import "SystemFocusContainment.js" as SystemFocusContainment

Item {
    id: root

    property Item backdrop: null          // wallpaper the glass blurs (Main.qml's `wall`)
    property Item focusReturnItem: null

    // The one place the context property is touched (absent when the feature is compiled out).
    readonly property var sharing: typeof dlnaSharing !== "undefined" ? dlnaSharing : null
    readonly property bool available: !!sharing
    readonly property bool isOn: available && sharing.enabled === true
    readonly property bool isRunning: available && sharing.running === true
    readonly property bool isBusy: available && sharing.busy === true
    readonly property string errorText: available && sharing.error ? String(sharing.error) : ""
    readonly property string friendlyName: available && sharing.friendlyName
                                           ? String(sharing.friendlyName) : ""
    readonly property string networkLabel: available && sharing.networkLabel
                                           ? String(sharing.networkLabel) : ""
    readonly property int itemCount: available && sharing.itemCount ? Number(sharing.itemCount) : 0
    readonly property var shareTypes: available && sharing.shareTypes ? sharing.shareTypes : []

    visible: false
    anchors.fill: parent
    z: 900

    property real anchorRight: -1
    property real anchorBottom: -1
    property int edgeMargin: 16
    property int anchorGap: 8

    Theme { id: theme }

    function itemsLine() {
        const n = root.itemCount
        const formatted = Number(n).toLocaleString(Qt.locale(), "f", 0)
        return n === 1 ? qsTr("%1 item").arg(formatted) : qsTr("%1 items").arg(formatted)
    }
    function typesLine() {
        const list = root.shareTypes
        const parts = []
        for (let i = 0; i < list.length; ++i)
            parts.push(String(list[i]))
        return parts.join(" · ")
    }

    function rememberInvoker() {
        const active = root.Window.window ? root.Window.window.activeFocusItem : null
        if (active && !SystemFocusContainment.isWithin(active, root))
            focusReturnItem = active
    }
    function restoreInvoker() {
        const target = focusReturnItem
        focusReturnItem = null
        Qt.callLater(function() {
            if (target && target.visible && target.enabled)
                target.forceActiveFocus()
        })
    }
    function open() {
        if (!root.visible)
            rememberInvoker()
        root.visible = true
        Qt.callLater(function() {
            if (root.visible)
                shareSwitch.forceActiveFocus(Qt.PopupFocusReason)
        })
    }
    function close(restoreFocus) {
        if (!root.visible)
            return
        root.visible = false
        if (restoreFocus !== false)
            restoreInvoker()
    }
    function toggle() { root.visible ? root.close() : root.open() }
    function toggleAt(right, bottom) {
        root.anchorRight = right
        root.anchorBottom = bottom
        root.toggle()
    }
    function flip() {
        if (root.available)
            root.sharing.setEnabled(!root.isOn)
    }

    // Full-window click-catcher: any tap outside the card closes.
    MouseArea {
        anchors.fill: parent
        onClicked: root.close()
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
            root.close()
            event.accepted = true
        } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
            // One focusable control: keep focus inside the card while it is open.
            event.accepted = true
        }
    }

    Glass {
        id: card
        backdrop: root.backdrop
        radius: 14
        tint: 0.06
        scrim: 0.38
        blurMax: 48
        x: root.anchorRight >= 0
           ? Math.max(root.edgeMargin, Math.min(root.width - width - root.edgeMargin, root.anchorRight - width))
           : root.width - width - 96
        y: root.anchorBottom >= 0
           ? Math.max(root.edgeMargin, Math.min(root.height - height - root.edgeMargin, root.anchorBottom + root.anchorGap))
           : 66
        width: 300
        height: col.implicitHeight + 32

        // swallow taps on the card itself so they never reach the click-catcher
        MouseArea { anchors.fill: parent }

        Column {
            id: col
            x: 18; y: 16
            width: parent.width - 36
            spacing: 0

            // ---- title + switch ----
            Item {
                width: parent.width
                height: 24
                Image {
                    id: headGlyph
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20; height: 20
                    source: root.isRunning ? "../assets/icons/access-point-gold.svg"
                                           : "../assets/icons/access-point.svg"
                    sourceSize.width: 40; sourceSize.height: 40
                    fillMode: Image.PreserveAspectFit
                    opacity: root.isRunning ? 1.0 : 0.72
                }
                Text {
                    anchors.left: headGlyph.right
                    anchors.leftMargin: 10
                    anchors.right: shareSwitch.left
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Share on your Wi-Fi")
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 15
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                KeyboardAction {
                    id: shareSwitch
                    objectName: "dlnaShareSwitch"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 36; height: 20
                    enabled: root.available
                    accessibleName: qsTr("Share on your Wi-Fi")
                    accessibleDescription: root.isOn ? qsTr("On") : qsTr("Off")
                    focusRadius: 10
                    onTriggered: root.flip()
                    Accessible.role: Accessible.CheckBox
                    Accessible.checkable: true
                    Accessible.checked: root.isOn

                    Rectangle {
                        anchors.fill: parent
                        radius: 10
                        color: root.isOn ? theme.gold : Qt.rgba(1, 1, 1, 0.22)
                        opacity: shareSwitch.enabled ? 1.0 : 0.5
                        Behavior on color { ColorAnimation { duration: 140 } }
                        Rectangle {
                            width: 16; height: 16; radius: 8
                            y: 2
                            x: root.isOn ? parent.width - width - 2 : 2
                            color: root.isOn ? "#14130f" : theme.ink
                            Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                        }
                    }
                }
            }

            // ---- warning ----
            Text {
                width: parent.width
                topPadding: 10
                text: qsTr("Anyone on this network can see and download what's shared.")
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 13
                lineHeight: 1.25
                wrapMode: Text.WordWrap
            }

            // ---- details ----
            Item { width: 1; height: 14 }
            Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.12) }
            Item { width: 1; height: 10 }

            Column {
                width: parent.width
                spacing: 4

                Item {
                    visible: root.friendlyName.length > 0
                    width: parent.width
                    height: 22
                    Text {
                        id: nameLabel
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("TVs see it as")
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 13
                    }
                    Text {
                        anchors.left: nameLabel.right
                        anchors.leftMargin: 12
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignRight
                        text: root.friendlyName
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                }

                Item {
                    visible: root.isRunning
                    width: parent.width
                    height: 22
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Sharing")
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 13
                    }
                    Text {
                        objectName: "dlnaShareItemCount"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.itemsLine()
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                    }
                }

                Text {
                    visible: root.isRunning && root.typesLine().length > 0
                    width: parent.width
                    text: root.typesLine()
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                Text {
                    objectName: "dlnaShareBusy"
                    visible: root.isBusy
                    width: parent.width
                    text: qsTr("Starting…")
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 12
                }

                Text {
                    objectName: "dlnaShareError"
                    visible: root.errorText.length > 0
                    width: parent.width
                    text: root.errorText
                    color: "#f2b27a"
                    font.family: theme.ui
                    font.pixelSize: 12
                    lineHeight: 1.2
                    wrapMode: Text.WordWrap
                }

                Text {
                    objectName: "dlnaShareNetwork"
                    visible: root.isRunning && root.networkLabel.length > 0
                    width: parent.width
                    topPadding: 6
                    text: root.networkLabel
                    color: Qt.rgba(1, 1, 1, 0.42)
                    font.family: theme.ui
                    font.pixelSize: 11
                }
            }
        }
    }
}
