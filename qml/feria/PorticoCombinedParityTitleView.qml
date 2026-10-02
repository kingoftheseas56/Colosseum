import QtQuick
import QtQuick.Layouts
import "PorticoData.js" as Data
import ".." as Colosseum

Item {
    id: root
    required property var controller
    readonly property real u: controller.unit
    readonly property real m: controller.marginX
    property var it: controller.titleObj(controller.selectedTitle)
    property var doors: controller.offers(it)
    property var related: controller.relatedTitles(it)

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position:0; color:controller.dusk }
            GradientStop { position:1; color:controller.night }
        }
    }
    Image {
        id: bg
        anchors { left:parent.left; right:parent.right; top:parent.top }
        height: parent.height * 0.86
        source: controller.artUrl(root.it, true)
        fillMode: Image.PreserveAspectCrop
        verticalAlignment: Image.AlignTop
        opacity: status === Image.Ready ? 0.5 : 0
        asynchronous: true
    }
    Rectangle {
        anchors.fill: bg
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position:0; color:Qt.rgba(6/255,7/255,11/255,0.94) }
            GradientStop { position:0.5; color:Qt.rgba(6/255,7/255,11/255,0.60) }
            GradientStop { position:1; color:Qt.rgba(6/255,7/255,11/255,0.25) }
        }
    }
    Rectangle {
        anchors { left:bg.left; right:bg.right; bottom:bg.bottom }
        height: bg.height * 0.6
        gradient: Gradient {
            GradientStop { position:0; color:"transparent" }
            GradientStop { position:1; color:controller.night }
        }
    }
    Rectangle {
        x:m; y:1.8*u; height:2.5*u; width:back.implicitWidth + 1.3*u; radius:height/2
        color:Qt.rgba(0,0,0,0.45)
        border.width:1
        border.color:Qt.rgba(1,1,1,0.16)
        z:5
        Colosseum.BackAction {
            id: back
            anchors.centerIn: parent
            variant: "plain"
            label: "Portico"
            idleColor: controller.mist
            hoverColor: controller.ink
            labelSize: 0.9*u
            onTriggered: controller.back()
        }
    }

    Flickable {
        id: scroll
        anchors.fill: parent
        contentWidth: width
        contentHeight: body.implicitHeight + 15*u
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Row {
            id: body
            x:m; y:7*u
            spacing:3*u

            Rectangle {
                width:15*u; height:22.5*u; radius:0.9*u
                color:Qt.rgba(1,1,1,0.05)
                border.width:1; border.color:Qt.rgba(1,1,1,0.10)
                clip:true
                Rectangle {
                    anchors.fill:parent; radius:parent.radius
                    gradient:Gradient {
                        orientation:Gradient.Horizontal
                        GradientStop { position:0; color:controller.toneFor(controller.selectedTitle)[0] }
                        GradientStop { position:1; color:controller.toneFor(controller.selectedTitle)[1] }
                    }
                    Text {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 0.9*u }
                        text: root.it ? root.it.t : ""
                        color: controller.ink
                        font.family: controller.displayFont
                        font.pixelSize: 1.7*u
                        wrapMode: Text.WordWrap
                        maximumLineCount: 5
                    }
                }
                Image { anchors.fill:parent; source:controller.artUrl(root.it,false); fillMode:Image.PreserveAspectCrop; asynchronous:true; visible:status===Image.Ready }
            }

            Column {
                width:Math.min(56*u, scroll.width - 18*u - 2*m)
                spacing:0.65*u
                Text { text:root.it?Data.KIND[root.it.k]:""; color:controller.mist; font.family:controller.uiFont; font.pixelSize:1*u }
                Text {
                    width:parent.width
                    text:root.it?root.it.t:""
                    color:controller.ink
                    font.family:controller.displayFont
                    font.pixelSize: root.it && root.it.t.length>40 ? 2.3*u : (root.it && root.it.t.length>22 ? 3*u : 4.2*u)
                    font.weight:Font.Medium
                    lineHeight:1.02
                    wrapMode:Text.WordWrap
                }
                Text { text:root.it?root.it.by:""; color:controller.mist; font.family:controller.uiFont; font.pixelSize:1.15*u }
                Row {
                    spacing:1.2*u
                    Text {
                        text: root.it ? root.it.y : ""
                        color: "#f7f7f5"
                        font.family: controller.uiFont
                        font.pixelSize: 0.95*u
                    }
                    Repeater {
                        model: root.it ? root.it.f : []
                        delegate: Text {
                            required property string modelData
                            text: modelData
                            color: "#c9c8d0"
                            font.family: controller.uiFont
                            font.pixelSize: 0.95*u
                        }
                    }
                }
                Text {
                    width:Math.min(40*u,parent.width)
                    text:root.it?root.it.s:""
                    color:controller.mist
                    font.family:controller.uiFont
                    font.pixelSize:1.05*u
                    lineHeight:1.6
                    wrapMode:Text.WordWrap
                }
                Text { topPadding:1.2*u; text:"Where to " + (root.it?Data.VERB[root.it.k]:"watch"); color:controller.ink; font.family:controller.displayFont; font.pixelSize:1.55*u; font.weight:Font.Medium }
                Grid {
                    id: doorGrid
                    width:parent.width
                    columns: parent.width >= 42*u ? 2 : 1
                    spacing:0.8*u
                    Repeater {
                        model:root.doors
                        delegate:Rectangle {
                            required property var modelData
                            required property int index
                            width: (doorGrid.width - (doorGrid.columns - 1) * doorGrid.spacing) / doorGrid.columns
                            height:5*u
                            radius:1*u
                            color:index===controller.titleActionIndex?Qt.rgba(1,1,1,0.09):Qt.rgba(1,1,1,0.045)
                            border.width:index===controller.titleActionIndex?0.1875*u:1
                            border.color:index===controller.titleActionIndex?controller.gold:Qt.rgba(1,1,1,0.10)
                            RowLayout {
                                anchors{fill:parent;leftMargin:0.9*u;rightMargin:1.1*u}
                                spacing:1*u
                                Rectangle {
                                    Layout.preferredWidth:3.2*u;Layout.preferredHeight:3.2*u;radius:0.85*u;color:Qt.rgba(1,1,1,0.05);border.width:1;border.color:Qt.rgba(1,1,1,0.10)
                                    PorticoCombinedGlyph { anchors.centerIn:parent;width:1.7*u;height:1.7*u;glyphKey:modelData.pk;tone:controller.ink }
                                }
                                ColumnLayout {
                                    Layout.fillWidth:true;spacing:0.2*u
                                    Text { text:controller.providerName(modelData.pk); color:controller.ink; font.family:controller.uiFont; font.pixelSize:1.08*u; font.weight:Font.DemiBold; elide:Text.ElideRight; Layout.fillWidth:true }
                                    Text { text:modelData.level==="t"?"Opens the " + (root.it?Data.KIND[root.it.k].toLowerCase():"title") + " page":"Search results for this title"; color:controller.slate; font.family:controller.uiFont; font.pixelSize:0.84*u; elide:Text.ElideRight; Layout.fillWidth:true }
                                }
                                Text { text:controller.activeApps.indexOf(modelData.pk)>=0?"In your apps":""; color:controller.mist; font.family:controller.uiFont; font.pixelSize:0.78*u }
                            }
                            MouseArea { anchors.fill:parent; hoverEnabled:true; onEntered:controller.titleActionIndex=index; onClicked:controller.openHost(modelData.pk,controller.selectedTitle,modelData.level==="t"?"title":"search") }
                        }
                    }
                }
                Row {
                    topPadding:0.4*u
                    spacing:0.7*u
                    Rectangle {
                        width:6*u;height:2.9*u;radius:0.8*u
                        color:controller.titleActionIndex===root.doors.length?Qt.rgba(1,1,1,0.09):Qt.rgba(1,1,1,0.06)
                        border.width:controller.titleActionIndex===root.doors.length?0.1875*u:1
                        border.color:controller.titleActionIndex===root.doors.length?controller.gold:Qt.rgba(1,1,1,0.14)
                        Text { anchors.centerIn:parent; text:controller.saved.indexOf(controller.selectedTitle)>=0?"Saved":"Save";color:controller.ink;font.family:controller.uiFont;font.pixelSize:0.95*u }
                        MouseArea { anchors.fill:parent; hoverEnabled:true; onEntered:controller.titleActionIndex=root.doors.length; onClicked:controller.toggleSaved(controller.selectedTitle) }
                    }
                }
                Text {
                    visible:root.related.length>0
                    topPadding:1.1*u
                    text:root.it && root.it.k==="artist"?"Albums":"In other media"
                    color:controller.ink
                    font.family:controller.displayFont
                    font.pixelSize:1.55*u
                    font.weight:Font.Medium
                }
                Row {
                    visible:root.related.length>0
                    spacing:1.25*u
                    Repeater {
                        model:root.related
                        delegate:PorticoCombinedParityMediaCard {
                            required property string modelData
                            required property int index
                            property var rel:controller.titleObj(modelData)
                            unit:u;width:9.25*u
                            title:rel?rel.t:"";sub:rel?rel.y:"";artSource:controller.artUrl(rel,false);shape:controller.shapeFor(rel)
                            selected:controller.titleActionIndex===root.doors.length+1+index
                            toneA:controller.toneFor(modelData)[0];toneB:controller.toneFor(modelData)[1]
                            displayFont:controller.displayFont;ink:controller.ink;mist:controller.mist;slate:controller.slate;gold:controller.gold
                            onEntered:controller.titleActionIndex=root.doors.length+1+index
                            onTriggered:controller.openTitle(modelData)
                        }
                    }
                }
                Text {
                    topPadding:1*u
                    width:44*u
                    text:"Listings come from public catalogues and can lag. Whether it plays, and whether it's included in your plan, is up to the service."
                    color:controller.slate
                    font.family:controller.uiFont
                    font.pixelSize:0.82*u
                    lineHeight:1.5
                    wrapMode:Text.WordWrap
                }
            }
        }
    }
}
