import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "PorticoData.js" as Data
import ".." as Colosseum

Item {
    id: root
    required property var controller
    readonly property real u: controller.unit
    readonly property real m: controller.marginX
    property alias searchField: input

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(6/255,7/255,11/255,0.985)
    }

    Flickable {
        id: scroll
        anchors.fill: parent
        contentWidth: width
        contentHeight: body.implicitHeight + 9*u
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: body
            x:m; y:2.2*u
            width:scroll.width - 2*m
            spacing:0.9*u

            Rectangle {
                width:parent.width
                height:4.6*u
                radius:1.3*u
                color:Qt.rgba(1,1,1,0.05)
                border.width: input.activeFocus ? 0.1875*u : 1
                border.color: input.activeFocus ? controller.gold : Qt.rgba(1,1,1,0.14)
                RowLayout {
                    anchors{fill:parent;leftMargin:1.4*u;rightMargin:1.4*u}
                    spacing:1*u
                    Image {
                        Layout.preferredWidth:1.6*u;Layout.preferredHeight:1.6*u
                        source:Qt.resolvedUrl("../../assets/icons/search.svg")
                        opacity:0.8
                    }
                    TextField {
                        id:input
                        Layout.fillWidth:true
                        Layout.fillHeight:true
                        text:controller.query
                        placeholderText:"A film, a show, an album, an artist, a book…"
                        color:controller.ink
                        placeholderTextColor:controller.slate
                        font.family:controller.displayFont
                        font.pixelSize:2.1*u
                        background:null
                        selectByMouse:true
                        onTextEdited:{controller.query=text;controller.searchResultIndex=0}
                        Keys.onEscapePressed:controller.back()
                        Keys.onDownPressed:{controller.searchResultIndex=Math.min(controller.visibleResults().length+controller.searchableApps().length-1,controller.searchResultIndex+1)}
                        Keys.onUpPressed:{controller.searchResultIndex=Math.max(0,controller.searchResultIndex-1)}
                        Keys.onReturnPressed:controller.activateSearchChoice()
                        Keys.onEnterPressed:controller.activateSearchChoice()
                    }
                }
            }
            Text {
                text:"Searching Portico's catalogues and charts. Esc closes."
                color:controller.slate
                font.family:controller.uiFont
                font.pixelSize:0.88*u
                leftPadding:0.2*u
            }

            Item {
                width:parent.width
                height:controller.query.trim().length ? resultsColumn.implicitHeight : emptyColumn.implicitHeight

                Column {
                    id:emptyColumn
                    width:parent.width
                    visible:controller.query.trim().length===0
                    spacing:1.2*u
                    Text { topPadding:0.7*u;text:"Try";color:controller.slate;font.family:controller.uiFont;font.pixelSize:0.95*u }
                    Flow {
                        width:parent.width;spacing:0.6*u
                        Repeater {
                            model:Data.TRY
                            delegate:PorticoCombinedPill {
                                required property string modelData
                                unit:u;compact:true;label:modelData
                                onTriggered:{controller.query=modelData;input.text=modelData;controller.searchResultIndex=0}
                            }
                        }
                    }
                }

                Column {
                    id:resultsColumn
                    width:parent.width
                    visible:controller.query.trim().length>0
                    spacing:1.1*u
                    property var hits: controller.visibleResults()
                    property var topItem: hits.length ? controller.titleObj(hits[0]) : null

                    Rectangle {
                        visible:resultsColumn.topItem!==null
                        width:parent.width
                        height:12.8*u
                        radius:1.3*u
                        color:Qt.rgba(1,1,1,0.03)
                        border.width:controller.searchResultIndex===0?0.1875*u:1
                        border.color:controller.searchResultIndex===0?controller.gold:Qt.rgba(1,1,1,0.08)
                        Row {
                            anchors{fill:parent;margins:1*u}
                            spacing:1.6*u
                            Rectangle {
                                width:9*u;height:10.8*u;radius:0.7*u;clip:true;color:Qt.rgba(1,1,1,0.05)
                                Image{anchors.fill:parent;source:controller.artUrl(resultsColumn.topItem,false);fillMode:Image.PreserveAspectCrop;asynchronous:true;visible:status===Image.Ready}
                            }
                            Column {
                                width:parent.width-11*u;anchors.verticalCenter:parent.verticalCenter;spacing:0.45*u
                                Text{text:resultsColumn.topItem?Data.KIND[resultsColumn.topItem.k]+"   "+resultsColumn.topItem.y:"";color:controller.mist;font.family:controller.uiFont;font.pixelSize:0.95*u}
                                Text{text:resultsColumn.topItem?resultsColumn.topItem.t:"";color:controller.ink;font.family:controller.displayFont;font.pixelSize:2.4*u;font.weight:Font.Medium}
                                Text{width:Math.min(parent.width,44*u);text:resultsColumn.topItem?resultsColumn.topItem.s:"";color:controller.mist;font.family:controller.uiFont;font.pixelSize:0.95*u;wrapMode:Text.WordWrap;maximumLineCount:2;elide:Text.ElideRight}
                                Text{text:resultsColumn.topItem?"On " + controller.offers(resultsColumn.topItem).map(function(d){return controller.providerName(d.pk)}).join(", "):"";color:controller.slate;font.family:controller.uiFont;font.pixelSize:0.88*u}
                            }
                        }
                        MouseArea{anchors.fill:parent;hoverEnabled:true;onEntered:controller.searchResultIndex=0;onClicked:if(resultsColumn.hits.length)controller.openTitle(resultsColumn.hits[0])}
                    }

                    Text {
                        visible:resultsColumn.hits.length===0
                        text:"Nothing in the catalogues for “"+controller.query+"”"
                        color:controller.mist
                        font.family:controller.displayFont
                        font.pixelSize:1.7*u
                    }

                    Text { text:"Search an app directly"; color:controller.slate;font.family:controller.uiFont;font.pixelSize:0.95*u;topPadding:0.5*u }
                    Flow {
                        width:parent.width;spacing:0.6*u
                        Repeater {
                            model:controller.searchableApps()
                            delegate:PorticoCombinedPill {
                                required property string modelData
                                required property int index
                                unit:u;compact:true;label:controller.providerName(modelData)
                                focusedState:controller.searchResultIndex===resultsColumn.hits.length+index
                                onEntered:controller.searchResultIndex=resultsColumn.hits.length+index
                                onTriggered:controller.openHost(modelData,"","search")
                            }
                        }
                    }

                    Text { visible:resultsColumn.hits.length>1; text:"More in Portico"; color:controller.ink; font.family:controller.displayFont; font.pixelSize:1.75*u; topPadding:0.7*u }
                    Row {
                        visible:resultsColumn.hits.length>1
                        spacing:1.25*u
                        Repeater {
                            model:resultsColumn.hits.slice(1,8)
                            delegate:PorticoCombinedMediaCard {
                                required property string modelData
                                required property int index
                                property var resultItem:controller.titleObj(modelData)
                                unit:u;width:9.25*u
                                title:resultItem?resultItem.t:""
                                sub:resultItem?resultItem.y+"   "+Data.KIND[resultItem.k]:""
                                artSource:controller.artUrl(resultItem,false)
                                shape:controller.shapeFor(resultItem)
                                selected:controller.searchResultIndex===index+1
                                toneA:controller.toneFor(modelData)[0];toneB:controller.toneFor(modelData)[1]
                                displayFont:controller.displayFont;ink:controller.ink;mist:controller.mist;slate:controller.slate;gold:controller.gold
                                onEntered:controller.searchResultIndex=index+1
                                onTriggered:controller.openTitle(modelData)
                            }
                        }
                    }
                }
            }
        }
    }

    Component.onCompleted: if (visible) Qt.callLater(function(){ input.forceActiveFocus() })
    onVisibleChanged: if (visible) Qt.callLater(function(){ input.forceActiveFocus() })
}
