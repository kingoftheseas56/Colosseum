import QtQuick

Column {
    id:scaffold
    property real desktopPageMargin:54
    property real sidebarRailExtent:0
    readonly property alias layout:metrics
    readonly property real browseMargin:metrics.browseMargin
    readonly property var activeChoiceSheet:choices.activeSheet
    signal choicePicked(string purpose,string key)
    signal choiceDismissed(string purpose)
    function showChoices(title,options,key,purpose,invoker) { choices.showChoices(title,options,key,purpose,invoker) }
    PhoneChoiceHost { id:choices; onPicked:(purpose,key)=>scaffold.choicePicked(purpose,key); onDismissed:(purpose)=>scaffold.choiceDismissed(purpose) }
    BrowseLayout {
        id:metrics
        page:scaffold
        viewportWidth:scaffold.parent ? scaffold.parent.width : scaffold.width
        desktopMargin:scaffold.desktopPageMargin
        railExtent:scaffold.sidebarRailExtent
    }
}
