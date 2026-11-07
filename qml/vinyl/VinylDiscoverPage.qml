import QtQuick
import ".."

Item {
    id: root
    objectName: "vinylDiscoverPage"
    required property var worldData
    required property Item backdrop
    required property Flickable pageFlick
    property bool active: true
    property bool componentReady: false
    Component.onCompleted: componentReady=true
    readonly property var session: worldData.session || null
    readonly property bool profileReady: !!session && session.profileActive !== false && !session.shuttingDown
    readonly property real flowHeight: browser.flowHeight
    implicitHeight: flowHeight
    property alias currentType: browser.currentType
    readonly property real contentLeft: browser.contentLeft
    property alias items: browser.items
    property alias loading: browser.loading

    VinylMusicDiscoverAdapter {
        id: music
        browser: browser
        source: root.session ? root.session.source : null
        profileGeneration: root.session ? root.session.profileGeneration || 0 : 0
        onResetRequested: {
            if(!root.componentReady)return;
            browser.cancelPageRequest();browser.fetchGen++;
            browser.items=[];browser.typeStates=({});browser.noticeText="";
            Qt.callLater(browser.reloadCurrent);
        }
    }
    DiscoverBrowser {
        id: browser
        anchors.fill: parent
        automationPrefix: "vinyl"
        adapter: music
        active: root.active && root.profileReady
        pageFlick: root.pageFlick
        backdrop: root.backdrop
        fallbackType: "songs"
        posterVisualProfile: "gallery"
        textNoCatalogue: qsTr("No music charts are available.")
        textCatalogueEmpty: qsTr("This chart returned no music.")
        textFilterEmpty: qsTr("This chart returned no music.")
        onItemOpenRequested: item => root.worldData.activate(item.raw)
        onFetchGenChanged: music.cancelArt()
        onActiveChanged: if(!active) music.cancelArt()

        // Preserve the language when the shared picker selects a list.
        function _applyFilterKey(encoded) {
            if(!encoded.length) { clearFilter(); return; }
            var split=encoded.indexOf(_filterSep);
            var group=encoded.substring(0,split);
            var key=encoded.substring(split+_filterSep.length);
            if(group===music.languageGroup && key!==filters[group]) filters=({});
            setFilter(group,key);
        }
    }
}
