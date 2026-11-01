import QtQuick

ListView {
    property real posterWidth:150
    property real posterHeight:236
    property real captionHeight:56
    height:posterHeight+captionHeight+10
    orientation:ListView.Horizontal
    spacing:metrics.touch ? 16 : 22
    clip:true
    reuseItems:true
    boundsBehavior:Flickable.StopAtBounds
    AdaptiveLayout { id:metrics;page:parent }
}
