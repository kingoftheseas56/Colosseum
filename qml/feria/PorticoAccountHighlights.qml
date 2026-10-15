import QtQuick
import QtQuick.Layouts
import "PorticoData.js" as Data

Item {
    id: root
    required property var controller
    readonly property real u: controller.unit
    readonly property real m: controller.marginX
    property var sessions: controller.sampleSessions()
    function total() { var n=0;for(var i=0;i<sessions.length;i++)n+=sessions[i].mins;return n }
    function uniq(field) { var seen={};for(var i=0;i<sessions.length;i++){var v=sessions[i][field];if(v)seen[v]=true}return Object.keys(seen).length }

    Flickable {
        anchors.fill:parent
        contentWidth:width
        contentHeight:Math.max(height, portrait.height+4*u)
        clip:true

        Row {
            x:m;y:1*u
            width:parent.width-2*m
            spacing:2.8*u

            Column {
                id:portrait
                width:Math.min(34*u,(parent.width-2.8*u)*0.34)
                spacing:0.45*u

                Row {
                    spacing:0.5*u
                    Rectangle{width:5.8*u;height:2.5*u;radius:0.8*u;color:Qt.rgba(1,1,1,0.06);border.width:1;border.color:Qt.rgba(1,1,1,0.14);Text{anchors.centerIn:parent;text:"‹  Earlier";color:controller.mist;font.family:"Segoe UI";font.pixelSize:0.88*u}}
                    Rectangle{width:5.2*u;height:2.5*u;radius:0.8*u;color:Qt.rgba(1,1,1,0.06);border.width:1;border.color:Qt.rgba(1,1,1,0.14);opacity:0.4;Text{anchors.centerIn:parent;text:"Later  ›";color:controller.mist;font.family:"Segoe UI";font.pixelSize:0.88*u}}
                }
