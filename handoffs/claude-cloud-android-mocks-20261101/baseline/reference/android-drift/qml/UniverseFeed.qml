import QtQuick
import QtQuick.Controls

Flickable {
    id:feed
    property Item bodyItem:null
    property real tailPadding:0
    contentWidth:width
    contentHeight:bodyItem ? bodyItem.implicitHeight+tailPadding : 0
    clip:true
    boundsBehavior:Flickable.StopAtBounds
    flickableDirection:Flickable.VerticalFlick
    ScrollBar.vertical:HouseScrollBar { flick:feed }
    ScrollGlide { flick:feed }
}
