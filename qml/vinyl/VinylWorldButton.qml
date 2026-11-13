import QtQuick
import ".."
KeyboardAction {
    focusOnPointer: false
    id: root
    property string label: ""
    property string symbol: ""
    property bool selected: false
    property Item backdrop: null
    readonly property bool androidLabelledSymbol: androidPresentationEnabled && symbol.length > 0
        && symbol !== "play" && (symbol !== "cancel" || label === qsTr("Cancel import"))
    readonly property string androidShortLabel: {
        if (label === qsTr("Save mix to Library")) return qsTr("Save mix");
        if (label.indexOf(qsTr("Retry ")) === 0
                && [qsTr("Retry recommendations"), qsTr("Retry artist search"),
                    qsTr("Retry catalogue"), qsTr("Retry track"),
                    qsTr("Retry radio"), qsTr("Retry mix")].indexOf(label) < 0)
            return qsTr("Retry");
        return label;
    }
    // Fixed-width header buttons place their label toward the available side.
    readonly property bool androidCaptionOnLeft: androidLabelledSymbol && parent
        && width < caption.implicitWidth + 58
        && x + width + caption.implicitWidth + 8 > parent.width
    focusRadius: 12
    focusPrimary: selected
    implicitWidth: symbol.length > 0
        ? (androidLabelledSymbol ? caption.implicitWidth + 58 : 38)
        : caption.implicitWidth + 28
    implicitHeight: 38
    accessibleName: label
    tooltipText: root.symbol.length > 0 ? root.accessibleName : ""
    // Labels are drawn by the existing caption in this existing button wrapper.
    androidTooltipPolicy: "drop"
    Theme { id: theme }
    Glass {
        anchors.fill: parent
        backdrop: root.backdrop
        radius: 12
        tint: root.selected ? 0.16 : 0.06
        scrim: 0.18
    }
    Rectangle {
        anchors.fill: parent
        radius: 12
        color: root.selected ? theme.gold : root.interactionActive ? theme.glassTint : "transparent"
    }
    Text {
        id: caption
        anchors.verticalCenter: parent.verticalCenter
        x: root.androidLabelledSymbol
            ? (root.androidCaptionOnLeft ? -implicitWidth - 8 : 40)
            : (parent.width - implicitWidth) / 2
        text: root.androidLabelledSymbol ? root.androidShortLabel : root.label
        visible: root.symbol.length === 0 || root.androidLabelledSymbol
        color: root.selected ? theme.biblioWashBottom : theme.ink
        opacity: root.enabled ? 1 : 0.35
        font.family: theme.ui
        font.pixelSize: 13
        font.weight: root.selected ? Font.DemiBold : Font.Normal
    }
    VinylControlIcon {
        id: controlIcon
        anchors.verticalCenter: parent.verticalCenter
        x: root.androidLabelledSymbol
            ? (root.width < caption.implicitWidth + 58 ? (root.width - width) / 2 : 12)
            : (parent.width - width) / 2
        symbol: root.symbol
        color: root.selected ? theme.biblioWashBottom : theme.ink
        visible: root.symbol.length > 0
        opacity: root.enabled ? 1 : 0.35
    }
}
