// Tooltip look precedent (2026-11-14). Run with Qt's qml tool:
//   C:\Qt\6.11.1\msvc2022_64\bin\qml.exe tests\qml\tooltip_style_preview.qml -- <out-dir>
// Renders real Colosseum content (wallpaper, a hero banner, top-bar icons) under the
// production TooltipStyle + KeyboardAction, shows a short and a long tooltip in turn,
// saves a screenshot of each to <out-dir>, then quits.
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "../../qml" as App

Window {
    id: win
    width: 1280; height: 720; visible: true
    color: "#05060a"
    title: "Colosseum tooltip preview"
    readonly property string outDir: Qt.application.arguments.length > 0
        ? Qt.application.arguments[Qt.application.arguments.length - 1] : "."

    Image { anchors.fill: parent; source: "../../assets/wallpaper/cold-ripple.jpg"; fillMode: Image.PreserveAspectCrop }
    Image {
        x: 60; y: 120; width: 1160; height: 420
        source: "../../assets/universes/one-piece/east-blue/banners/arlong.png"
        fillMode: Image.PreserveAspectCrop
    }
    Row {
        anchors.right: parent.right; anchors.rightMargin: 120; y: 200; spacing: 24
        Repeater {
            model: [
                { icon: "update.svg", tip: "Updates" },
                { icon: "trackers.svg", tip: "Trackers" },
                { icon: "settings.svg", tip: "Smart Pick starts a streaming source automatically at your preferred quality. Turn it off to choose a source yourself." }
            ]
            delegate: Item {
                width: 36; height: 36
                property alias action: act
                Image { anchors.centerIn: parent; width: 26; height: 26; source: "../../assets/icons/" + modelData.icon }
                App.KeyboardAction {
                    id: act
                    anchors.fill: parent
                    accessibleName: modelData.tip
                    tooltipText: modelData.tip
                    longPressTooltipEnabled: false
                }
            }
        }
    }

    App.TooltipStyle { id: tooltipStyle; backdrop: win.contentItem }

    property int step: 0
    Timer {
        interval: 900; repeat: true; running: true
        onTriggered: {
            const icons = iconRow()
            if (win.step === 0) { icons[1].action.tooltipHovered = true }
            else if (win.step === 1) { win.contentItem.grabToImage(function(r) { r.saveToFile(win.outDir + "/tooltip-short.png") }) }
            else if (win.step === 2) { icons[1].action.tooltipHovered = false; icons[2].action.tooltipHovered = true }
            else if (win.step === 3) { win.contentItem.grabToImage(function(r) { r.saveToFile(win.outDir + "/tooltip-long.png") }) }
            else { Qt.quit() }
            win.step++
        }
    }
    function iconRow() {
        const row = win.contentItem.children.find(c => c instanceof Row)
        const out = []
        for (let i = 0; i < row.children.length; ++i)
            if (row.children[i].action) out.push(row.children[i])
        return out
    }
}
