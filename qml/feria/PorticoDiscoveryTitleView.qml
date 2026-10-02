import QtQuick
import QtQuick.Layouts
import "PorticoData.js" as Data
import ".." as Colosseum

Item {
    id: root
    objectName: "porticoTitleView"
    required property var controller
    readonly property real u: controller.unit
    readonly property real m: controller.marginX
    property var it: controller.titleObj(controller.selectedTitle)
    property var doors: controller.offers(it)
    property var related: controller.relatedTitles(it)
    // A direct URL identifies a title page; only an offer source may confirm availability.
    readonly property var offerGroups: buildOfferGroups()

    function groupFor(door) {
        if (door.appOnly || door.reason === "app-only") return "app"
        if (door.availabilityConfirmed === true) return "available"
        if (door.fallback || door.level === "s" || door.reason === "search" ||
                (door.exact === false && door.level !== "t")) return "search"
        return "direct"
    }
    function buildOfferGroups() {
        var labels = {
            available:{ title:"Available from", note:"Offers listed for this title and region." },
            direct:{ title:"Direct title links", note:"A title link does not confirm playback or plan access." },
            search:{ title:"Search other services", note:"Search links do not confirm that the title is available there." },
            app:{ title:"App only", note:"These services need their own app." }
        }
        var groups = []
        for (var i = 0; i < doors.length; ++i) {
            var key = groupFor(doors[i])
            if (groups.length === 0 || groups[groups.length - 1].key !== key) {
                groups.push({ key:key, title:labels[key].title, note:labels[key].note, items:[] })
            }
            groups[groups.length - 1].items.push({ door:doors[i], sourceIndex:i })
        }
        return groups
    }
    function providerKey(door) { return door.pk || door.providerId || "" }
    function providerLabel(door) { return door.label || controller.providerName(providerKey(door)) }
    function offerDetail(door) {
        if (door.appOnly || door.reason === "app-only") return "Open in the service app"
        if (door.availabilityConfirmed === true) {
            var type = door.offerType || door.monetizationType || "Listed offer"
            return door.priceLabel ? type + " · " + door.priceLabel : type
        }
        if (groupFor(door) === "search") return "Availability unconfirmed"
        return "Title page link"
    }

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
    Flickable {
        id: scroll
        anchors.fill: parent
        contentWidth: width
        contentHeight: body.implicitHeight + 15*u
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Row {
            id: body
            x:m; y:Math.max(148, 11*u)
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
                        anchors { left:parent.left; right:parent.right; bottom:parent.bottom; margins:0.9*u }
                        text:root.it ? root.it.t : ""
                        color:controller.ink
                        font.family:controller.displayFont
                        font.pixelSize:1.7*u
                        wrapMode:Text.WordWrap
                        maximumLineCount:5
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
                    font.letterSpacing: -0.025 * (root.it && root.it.t.length>40 ? 2.3*u : (root.it && root.it.t.length>22 ? 3*u : 4.2*u))
                    font.weight:Font.Medium
                    lineHeight:1.02
                    wrapMode:Text.WordWrap
                }
                Text { text:root.it?root.it.by:""; color:controller.mist; font.family:controller.uiFont; font.pixelSize:1.15*u }
                Row {
                    spacing:1.2*u
                    Text {
                        text:root.it ? root.it.y : ""
                        color:controller.ink
                        font.family:controller.uiFont
                        font.pixelSize:0.95*u
                    }
                    Repeater {
                        model:root.it ? root.it.f : []
                        delegate:Text {
                            required property string modelData
                            text:modelData
                            color:controller.mist
                            font.family:controller.uiFont
                            font.pixelSize:0.95*u
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
                Text { visible:root.offerGroups.length===0; text:"No provider options are listed for this title yet."; color:controller.mist; font.family:controller.uiFont; font.pixelSize:0.92*u }
                Column {
                    id: offerSections
                    width:parent.width
                    spacing:1.15*u
                    Repeater {
                        model:root.offerGroups
                        delegate:Column {
                            id: offerSection
                            required property var modelData
                            width:offerSections.width
                            spacing:0.55*u
                            Text {
                                text:offerSection.modelData.title
                                color:controller.ink
                                font.family:controller.uiFont
                                font.pixelSize:1*u
                                font.weight:Font.DemiBold
                            }
                            Text {
                                width:parent.width
                                text:offerSection.modelData.note
                                color:controller.slate
                                font.family:controller.uiFont
                                font.pixelSize:0.84*u
                                wrapMode:Text.WordWrap
                            }
                            Grid {
                                id: doorGrid
                                width:offerSection.width
                                columns: width >= 42*u ? 2 : 1
                                spacing:0.8*u
                                Repeater {
                                    model:offerSection.modelData.items
                                    delegate:Rectangle {
                                        required property var modelData
                                        readonly property var door: modelData.door
                                        readonly property int sourceIndex: modelData.sourceIndex
                                        readonly property string pk: root.providerKey(door)
                                        width:(doorGrid.width - (doorGrid.columns - 1) * doorGrid.spacing) / doorGrid.columns
                                        height:5*u
                                        radius:1*u
                                        color:sourceIndex===controller.titleActionIndex?Qt.rgba(1,1,1,0.09):Qt.rgba(1,1,1,0.045)
                                        border.width:1
                                        border.color:Qt.rgba(1,1,1,0.10)
                                        Rectangle {
                                            anchors.fill:parent
                                            anchors.margins:-0.1875*u
                                            radius:parent.radius + 0.1875*u
                                            color:"transparent"
                                            border.width:0.1875*u
                                            border.color:controller.gold
                                            visible:sourceIndex===controller.titleActionIndex
                                            z:10
                                        }
                                        RowLayout {
                                            anchors { fill:parent; leftMargin:0.9*u; rightMargin:1.1*u }
                                            spacing:1*u
                                            Rectangle {
                                                Layout.preferredWidth:3.2*u; Layout.preferredHeight:3.2*u
                                                radius:0.85*u; color:Qt.rgba(1,1,1,0.05)
                                                border.width:1; border.color:Qt.rgba(1,1,1,0.10)
                                                PorticoCombinedGlyph { anchors.centerIn:parent; width:1.7*u; height:1.7*u; glyphKey:pk; tone:controller.ink }
                                            }
                                            ColumnLayout {
                                                Layout.fillWidth:true; spacing:0.2*u
                                                Text { text:root.providerLabel(door); color:controller.ink; font.family:controller.uiFont; font.pixelSize:1.08*u; font.weight:Font.DemiBold; elide:Text.ElideRight; Layout.fillWidth:true }
                                                Text { text:root.offerDetail(door); color:controller.slate; font.family:controller.uiFont; font.pixelSize:0.84*u; elide:Text.ElideRight; Layout.fillWidth:true }
                                            }
                                            Text { text:controller.activeApps.indexOf(pk)>=0?"In your apps":""; color:controller.mist; font.family:controller.uiFont; font.pixelSize:0.78*u }
                                        }
                                        MouseArea {
                                            anchors.fill:parent
                                            hoverEnabled:true
                                            onEntered:controller.titleActionIndex=sourceIndex
                                            onClicked: {
                                                controller.titleActionIndex=sourceIndex
                                                if (!door.appOnly && door.reason !== "app-only") controller.activateTitleAction()
                                            }
                                        }
                                    }
                                }
                            }
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
                        delegate:PorticoCombinedMediaCard {
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
