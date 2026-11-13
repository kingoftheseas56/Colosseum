pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ".."
import "VinylRecommendationActions.js" as RecommendationActions

Flickable {
    id: root
    objectName: "vinylRecommendationPage"
    required property var request
    required property var worldData
    required property Item backdrop
    property string error: ""
    readonly property var entries: request && Array.isArray(request.entries) ? request.entries : request && request.item ? [
        {
            item: request.item,
            options: request.options || []
        }
    ] : []
    readonly property var groups: RecommendationActions.groups(request && request.options || [])
    readonly property real inset: Math.min(54, width * 0.06)
    readonly property bool stacked: width < 640
    signal backRequested
    signal actionRequested(var ref, string action, int generation)
    clip: true
    contentWidth: width
    contentHeight: Math.max(height, content.y + content.height + 40)
    boundsBehavior: Flickable.StopAtBounds
    Theme {
        id: theme
    }
    ScrollGlide {
        flick: root
    }
    ScrollBar.vertical: HouseScrollBar {}
    function actionFor(key) {
        for (var i = 0; i < panels.count; i++) {
            var panel = panels.itemAt(i) as PreferencePanel;
            if (!panel)
                continue;
            var action = panel.controls.actionFor(key);
            if (action)
                return action;
        }
        return null;
    }
    BackAction {
        objectName: "vinylRecommendationBack"
        x: root.inset
        y: 0
        height: 44
        focusOnPointer: false
        label: qsTr("Back")
        onTriggered: root.backRequested()
    }
    Column {
        id: content
        x: root.inset
        y: 68
        width: root.width - 2 * x
        spacing: 32
        Text {
            width: parent.width
            text: qsTr("Recommendation preferences")
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 34
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            visible: !!root.error
            text: root.error
            textFormat: Text.PlainText
            color: theme.ink
            font.family: theme.ui
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            visible: root.entries.length === 0
            text: qsTr("No recommendation preferences yet.")
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 16
            wrapMode: Text.WordWrap
        }
        Repeater {
            id: panels
            model: root.entries
            delegate: PreferencePanel {}
        }
    }
    component PreferencePanel: Item {
        id: panel
        required property var modelData
        property alias controls: choices
        width: content.width
        height: Math.max(preview.height, choices.y + choices.height)
        VinylItemCard {
            id: preview
            objectName: "vinylRecommendationPreview"
            width: Math.min(196, parent.width)
            height: implicitHeight
            item: panel.modelData.item
            worldData: root.worldData
            pointerEnabled: false
            focusEnabled: false
            showFocusFrame: false
            backdrop: root.backdrop
        }
        VinylRecommendationControls {
            id: choices
            x: root.stacked ? 0 : preview.width + 54
            y: root.stacked ? preview.height + 28 : 0
            width: Math.max(0, parent.width - x)
            options: panel.modelData.options
            backdrop: root.backdrop
            pageFlick: root
            onActionRequested: (ref, action) => root.actionRequested(ref, action, root.request.generation)
        }
    }
}
