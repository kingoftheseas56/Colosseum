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
        reuseCardDelegates: true
        cardDelegate: Component {
            VinylItemCard {
                id: widget
                objectName: parent.objectName + "_widget"
                item: {
                    var raw = parent.modelItem && parent.modelItem.raw;
                    return raw ? Object.assign({}, raw, {albumTitle: raw.hints && raw.hints.album || ""}) : ({});
                }
                focusEnabled: false
                worldData: QtObject {
                    readonly property var session: root.session
                    function artwork(cover) { return cover || ""; }
                    function activate(item) { widget.parent.open(); }
                }
            }
        }
        textNoCatalogue: qsTr("No music charts are available.")
        textCatalogueEmpty: qsTr("This chart returned no music.")
        textFilterEmpty: filters[music.genreGroup] ? qsTr("No music matches this genre.") : textCatalogueEmpty
        onItemOpenRequested: item => root.worldData.activate(item.raw)
        onFetchGenChanged: {
            music.cancelArt();
            Qt.callLater(music.resumeWeekArt);
        }
        onActiveChanged: if(active) Qt.callLater(music.resumeWeekArt); else music.cancelArt()

        textPageRetry: music.catalogueKeyMissing ? qsTr("Try again.") : qsTr("Couldn't reach the catalogue. Click to try again.")

        function _applyFilterKey(encoded) {
            if(!encoded.length) { clearFilter(); return; }
            var split=encoded.indexOf(_filterSep);
            var group=encoded.substring(0,split);
            var key=encoded.substring(split+_filterSep.length);
            if(group===music.genreGroup && key==="all") { clearFilter(group); return; }
            setFilter(group,key);
        }
    }
}
