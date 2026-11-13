pragma ComponentBehavior: Bound
import QtQuick
import ".."
import "VinylRecommendationActions.js" as RecommendationActions

Column {
    id: root
    required property var options
    property Flickable pageFlick: null
    property Item backdrop: null
    readonly property var groups: RecommendationActions.groups(options)
    signal actionRequested(var ref, string action)
    spacing: 28
    Theme {
        id: theme
    }
    function actionFor(key) {
        for (var i = 0; i < sections.count; i++) {
            var section = sections.itemAt(i) as ActionGroup;
            if (!section)
                continue;
            for (var j = 0; j < section.actions.count; j++) {
                var action = section.actions.itemAt(j) as FeedbackControl;
                if (action.modelData.key === key)
                    return action;
            }
        }
        return null;
    }

    Repeater {
        id: sections
        model: root.groups
        delegate: ActionGroup {}
    }
    component ActionGroup: Column {
        id: section
        required property var modelData
        required property int index
        property alias actions: actions
        width: root.width
        spacing: 12
        Text {
            width: parent.width
            text: section.modelData.section === "item" ? qsTr("This recommendation") : section.modelData.title
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: theme.ink
            font.family: theme.ui
            font.pixelSize: 18
        }
        Grid {
            width: parent.width
            columns: Math.max(1, Math.floor((width + 12) / 56))
            spacing: 12
            Repeater {
                id: actions
                model: section.modelData.actions
                delegate: FeedbackControl { peers: actions }
            }
        }
    }
    component FeedbackControl: VinylWorldButton {
        id: control
        required property var modelData
        required property int index
        required property Repeater peers
        objectName: "vinylRecommendationAction_" + modelData.key
        width: 44
        height: 44
        label: control.modelData.label
        symbol: RecommendationActions.symbol(control.modelData.action)
        backdrop: root.backdrop
        KeyNavigation.left: index > 0 ? peers.itemAt(index - 1) : null
        KeyNavigation.right: index + 1 < peers.count ? peers.itemAt(index + 1) : null
        onActiveFocusChanged: if (activeFocus && root.pageFlick) {
            var position = mapToItem(root.pageFlick.contentItem, 0, 0);
            if (position.y < root.pageFlick.contentY)
                root.pageFlick.contentY = position.y;
            else if (position.y + height > root.pageFlick.contentY + root.pageFlick.height)
                root.pageFlick.contentY = position.y + height - root.pageFlick.height;
        }
        onTriggered: root.actionRequested(control.modelData.ref, control.modelData.action)
    }
}
