import QtQuick

Item {
    id:host
    property var choices:[]
    property string title:""
    property string selectedKey:""
    property string purpose:""
    property Item invoker:null
    property bool requested:false
    property int generation:0
    readonly property var activeSheet:loader.status===Loader.Ready ? loader.item : null
    signal picked(string purpose,string key)
    signal dismissed(string purpose)
    function showChoices(title,choices,key,purpose,invoker) {
        generation++
        host.title=title; host.choices=choices; selectedKey=key; host.purpose=purpose; host.invoker=invoker || null
        requested=true
        if (loader.status===Loader.Ready) loader.item.open()
    }
    Loader {
        id:loader
        active:host.requested
        onLoaded:item.open()
        sourceComponent:PhoneFilterSheet {
            title:host.title
            choices:host.choices
            selectedKey:host.selectedKey
            preferredFocusReturn:host.invoker
            onPicked:(key)=>host.picked(host.purpose,key)
            onClosed: {
                const generation=host.generation
                host.dismissed(host.purpose)
                Qt.callLater(function() { if (host.generation===generation) host.requested=false })
            }
        }
    }
}
