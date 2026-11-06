import QtQuick

FocusScope {
    id: root
    objectName: "dexterTributePage"
    property string extensionId: ""
    property string universeName: ""
    property bool reducedMotion: false
    readonly property var sceneView: tribute.item
    property bool focusPending: false
    property var motionOverride: undefined
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal watchRequested(var item)

    function takeKeyboardFocus() {
        focusPending = true
        if (sceneView) {
            sceneView.forceActiveFocus()
            focusPending = false
        } else {
            root.forceActiveFocus()
        }
    }

    function requestEscape() {
        if (!sceneView || sceneView.selectedIndex < 0) return false
        sceneView.keyboard(Qt.Key_Escape)
        return true
    }

    Rectangle { anchors.fill: parent; color: "#10090b" }

    Loader {
        id: tribute
        anchors.fill: parent
        asynchronous: true
        source: "dexter/TributeView.qml"
        onLoaded: {
            const view = root.sceneView
            view.reducedMotion = Qt.binding(function() {
                return root.motionOverride === undefined ? root.reducedMotion : root.motionOverride
            })
            view.reducedMotionToggleRequested.connect(function() {
                root.motionOverride = !view.reducedMotion
            })
            view.watchRequested.connect(root.watchRequested)
            if (root.focusPending) root.takeKeyboardFocus()
        }
    }

    BackAction {
        x: 28
        y: 24
        onTriggered: root.backRequested()
    }

    Text {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 440)
        visible: tribute.status !== Loader.Ready
        text: tribute.status === Loader.Error ? qsTr("This build could not load the Dexter 3D scene.")
                                              : qsTr("Opening the collection…")
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        color: "#ead8c5"
        font.pixelSize: 18
    }
}
