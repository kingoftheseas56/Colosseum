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
            portraits.clear();
            browser.cancelPageRequest();browser.fetchGen++;
            browser.items=[];browser.typeStates=({});browser.noticeText="";
            Qt.callLater(browser.reloadCurrent);
        }
    }
    // Like Theatre's poster wall, keep the decode in Qt's pixmap cache. A
    // recycled card drops its reference, so pin the 64 most recent decodes at
    // the same URL/size. These invisible Images share the existing pixmaps.
    Item {
        id: portraits
        visible: false
        property var goodSources: new Map()
        property var pins: new Map()
        function lastGood(id) { return goodSources.get(id) || ""; }
        function remember(id, source, size) {
            goodSources.set(id, String(source));
            var key = String(source) + "\n" + size.width + "x" + size.height;
            var pin = pins.get(key);
            if (pin) {
                pins.delete(key);
            } else {
                pin = pinFactory.createObject(portraits, {source: source, sourceSize: size});
            }
            pins.set(key, pin);
            if (pins.size > 64) {
                var oldest = pins.keys().next().value;
                pins.get(oldest).destroy();
                pins.delete(oldest);
            }
        }
        function clear() {
            goodSources = new Map();
            pins.forEach(pin => pin.destroy());
            pins.clear();
        }
        Component {
            id: pinFactory
            Image {
                cache: true
                asynchronous: true
                fillMode: Image.PreserveAspectCrop
            }
        }
    }
    DiscoverBrowser {
        id: browser
        anchors.fill: parent
        automationPrefix: "vinyl"
        modalFilterPopup: false
        adapter: music
        active: root.active && root.profileReady
        pageFlick: root.pageFlick
        backdrop: root.backdrop
        fallbackType: "songs"
        posterVisualProfile: "gallery"
        fixedGalleryWidth: true
        // Keep the 196 px tile on desktop, shrinking all three columns together in narrow windows.
        wallPosterWidth: Math.max(1, Math.min(196, Math.floor((width - contentLeft) / 3) - 20))
        reuseCardDelegates: true
        cardDelegateCaptionHeight: 55 // 196 × 251 at the standard card width
        cardDelegateDrawsFocus: true
        cardDelegate: Component {
            VinylItemCard {
                id: widget
                objectName: parent.objectName + "_widget"
                item: (parent.modelItem && parent.modelItem.raw) || ({})
                focusEnabled: false
                keyboardFocused: parent.keyboardFocused
                portraitCache: portraits
                // Grow into the cell gaps, not past the clipped grid's top/left edge.
                transformOrigin: Item.TopLeft
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
