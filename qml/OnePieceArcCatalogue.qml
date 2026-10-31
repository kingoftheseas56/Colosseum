pragma ComponentBehavior: Bound
import QtQuick
import "OnePieceCatalogApi.js" as CatalogApi

Item {
    id: root
    objectName: "onePieceArcCatalogue"

    required property var arc
    required property Item backdrop
    property string focusLane: "anime"
    property bool reducedMotion: false
    property var installedExtensions: []
    property var relatedItems: []
    property var tankobanCatalogRef: (typeof TankobanCatalog !== "undefined") ? TankobanCatalog : null
    property int mangaMalId: 13
    property bool _componentComplete: false
    property var _volumeRequestCatalog: null
    property int _volumeRequestMalId: 0
    property bool _volumeReentry: false
    readonly property bool phoneLayout: Qt.platform.os === "android"
                                        && (width < 600 || (width < 900 && height < 500))
    readonly property bool shortPhone: phoneLayout && height < 500
    readonly property string seaLabel: root.arc && root.arc.seaLabel ? String(root.arc.seaLabel) : "East Blue"
    readonly property string sagaLabel: root.arc && root.arc.sagaLabel ? String(root.arc.sagaLabel) : root.seaLabel

    property var animeItems: []
    property var onePaceItems: []
    property var liveItems: []
    property var volumeItems: []
    property bool animeLoading: false
    property bool onePaceLoading: false
    property bool onePaceInstalled: false
    property bool liveLoading: false

    signal backRequested()
    signal episodeRequested(var entry)
    signal mangaVolumeRequested(bool colorEdition, string volumeNumber)
    signal relatedRequested(var entry)

    Theme { id: theme }

    function countLabel(count, noun) {
        return String(count) + " " + noun + (count === 1 ? "" : "s")
    }

    function episodeRoute(item, lane) {
        var live = lane === "live"
        var pace = lane === "pace"
        var rootId = live ? "tt11737520" : (pace ? "pp_onepace" : "kitsu:12")
        return {
            id: rootId,
            type: "series",
            title: pace ? "One Pace" : "One Piece",
            cover: item && (item.poster || item.thumbnail) ? (item.poster || item.thumbnail) : "",
            art: item && (item.background || item.thumbnail) ? (item.background || item.thumbnail) : "",
            requestedArc: root.arc,
            requestedSeason: Number(item && item.season || 1),
            requestedEpisode: Number(item && item.episode || 1),
            requestedEpisodeRange: String(item && item.episode || ""),
            requestedProvider: item && item.provider ? item.provider : ""
        }
    }

    function refresh(revalidateVolumes) {
        if (!root._componentComplete) return
        if (!root.arc) {
            root._volumeReentry = true
            root.animeItems = []
            root.onePaceItems = []
            root.liveItems = []
            root.volumeItems = []
            root.animeLoading = false
            root.onePaceLoading = false
            root.liveLoading = false
            return
        }
        root.animeLoading = true
        root.onePaceLoading = true
        root.liveLoading = true
        CatalogApi.loadAnimeEpisodes(root.arc, function(rows) {
            root.animeItems = rows || []
            root.animeLoading = false
        })
        CatalogApi.loadOnePaceEpisodes(root.installedExtensions, root.arc, function(rows, installed) {
            root.onePaceItems = rows || []
            root.onePaceInstalled = installed === true
            root.onePaceLoading = false
            Qt.callLater(root.focusRequestedLane)
        })
        CatalogApi.loadLiveActionEpisodes(root.arc, function(rows) {
            root.liveItems = rows || []
            root.liveLoading = false
        })
        root.requestVolumes(revalidateVolumes !== false)
        Qt.callLater(root.focusRequestedLane)
    }

    function requestVolumes(revalidate) {
        if (!root._componentComplete) return
        root.rebuildVolumes()
        var catalog = root.tankobanCatalogRef
        if (!root.arc || root.mangaMalId <= 0 || !catalog
                || typeof catalog.requestSeries !== "function") return
        // Ordinary arc/extension changes only reselect this series. Explicit
        // refresh or re-entry reissues once: another consumer may have silently
        // superseded our request. The native owner replaces its single request
        // slot, so this does not create parallel requests or a signal retry loop.
        var shouldRequest = revalidate === true || root._volumeReentry
        root._volumeReentry = false
        if (root._volumeRequestCatalog === catalog
                && root._volumeRequestMalId === root.mangaMalId
                && !shouldRequest) return
        root._volumeRequestCatalog = catalog
        root._volumeRequestMalId = root.mangaMalId
        // Record before calling: cached results/failures can arrive inline.
        catalog.requestSeries(root.mangaMalId)
    }

    function rebuildVolumes() {
        if (!root.arc) {
            root.volumeItems = []
            return
        }
        var rows = []
        // ready() describes the local database, not remote or cached series.
        if (root.tankobanCatalogRef && root.mangaMalId > 0
                && typeof root.tankobanCatalogRef.volumes === "function")
            rows = root.tankobanCatalogRef.volumes(root.mangaMalId) || []
        root.volumeItems = rows.length
                ? CatalogApi.selectVolumes(rows, root.arc)
                : CatalogApi.fallbackVolumes(root.arc)
    }

    function focusRequestedLane() {
        var targetY = 0
        if (root.focusLane === "pace") targetY = paceBlock.y - 18
        else if (root.focusLane === "manga") targetY = mangaBlock.y - 18
        else if (root.focusLane === "color") targetY = colorBlock.y - 18
        else if (root.focusLane === "live") targetY = liveBlock.y - 18
        else targetY = animeBlock.y - 18
        catalogue.contentY = Math.max(0, Math.min(targetY,
                                  Math.max(0, catalogue.contentHeight - catalogue.height)))
    }

    onArcChanged: refresh(false)
    onInstalledExtensionsChanged: refresh(false)
    onTankobanCatalogRefChanged: {
        root._volumeRequestCatalog = null
        root.requestVolumes()
    }
    onMangaMalIdChanged: requestVolumes(true)
    onFocusLaneChanged: Qt.callLater(focusRequestedLane)
    Component.onCompleted: {
        root._componentComplete = true
        root.refresh(false)
    }

    Connections {
        target: root.tankobanCatalogRef
        ignoreUnknownSignals: true
        function onReadyChanged() { root.rebuildVolumes() }
        function onRevisionChanged() { root.rebuildVolumes() }
        function onSeriesChanged(malId) {
            if (malId === root.mangaMalId) root.rebuildVolumes()
        }
        function onSeriesFailed(malId, reason) {
            if (malId === root.mangaMalId) root.rebuildVolumes()
        }
        function onChanged() { root.rebuildVolumes() }
    }

    Rectangle {
        anchors.fill: parent
        color: "#07090c"
    }

    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: root.backdrop
        live: true
        hideSource: false
        visible: root.backdrop !== null
        opacity: 0.22
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.02, 0.025, 0.03, 0.72) }
            GradientStop { position: 0.34; color: Qt.rgba(0.02, 0.025, 0.03, 0.90) }
            GradientStop { position: 1.0; color: Qt.rgba(0.01, 0.012, 0.015, 0.98) }
        }
    }

    Flickable {
        id: catalogue
        anchors.fill: parent
        anchors.topMargin: root.shortPhone ? 72 : (root.phoneLayout ? 80 : 92)
        contentWidth: width
        contentHeight: contentCol.height + theme.margin
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: contentCol
            x: theme.margin
            width: catalogue.width - theme.margin * 2
            spacing: root.shortPhone ? 26 : (root.phoneLayout ? 34 : 46)

            Column {
                width: parent.width
                spacing: root.phoneLayout ? 5 : 7
                Text {
                    width: root.phoneLayout ? parent.width : implicitWidth
                    text: root.seaLabel.toUpperCase() + "  /  STORY CATALOGUE"
                    color: theme.gold
                    font.family: theme.ui
                    font.pixelSize: theme.fontSize(root.phoneLayout ? 9 : 10)
                    font.bold: true
                    font.letterSpacing: root.phoneLayout ? 1.7 : 2.6
                }
                Text {
                    width: root.phoneLayout ? parent.width : implicitWidth
                    text: root.arc ? root.arc.title : ""
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: theme.fontSize(root.shortPhone ? 28 : (root.phoneLayout ? 34 : 44))
                    font.bold: true
                    wrapMode: root.phoneLayout ? Text.WordWrap : Text.NoWrap
                    lineHeight: root.phoneLayout ? 1.02 : 1.0
                }
                Text {
                    width: root.phoneLayout ? parent.width : Math.min(parent.width * 0.76, 900)
                    text: root.arc ? root.arc.summary || "" : ""
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: theme.fontSize(root.phoneLayout ? 12 : 13)
                    wrapMode: Text.WordWrap
                    lineHeight: 1.25
                }
            }

            Column {
                id: animeBlock
                width: parent.width
                spacing: 18
                WidgetHeader {
                    width: parent.width
                    title: "Anime"
                    sub: root.animeLoading ? "Anime Kitsu · loading"
                         : root.countLabel(root.animeItems.length, "episode") + " · Anime Kitsu"
                    navigable: false
                }
                Flickable {
                    width: parent.width
                    height: 216
                    contentWidth: animeRow.width
                    contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: animeRow
                        spacing: 20
                        Repeater {
                            model: root.animeItems
                            delegate: OnePieceEpisodeCard {
                                required property var modelData
                                entry: modelData
                                sourceLabel: "ANIME"
                                onActivated: root.episodeRequested(root.episodeRoute(modelData, "anime"))
                            }
                        }
                    }
                }
            }


            Column {
                id: paceBlock
                width: parent.width
                spacing: 18
                visible: root.onePaceInstalled
                WidgetHeader {
                    width: parent.width
                    title: "One Pace"
                    sub: root.onePaceLoading ? "One Pace Addon · loading"
                         : root.countLabel(root.onePaceItems.length, "edit") + " · One Pace Addon"
                    navigable: false
                }
                Flickable {
                    width: parent.width
                    height: 216
                    contentWidth: paceRow.width
                    contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: paceRow
                        spacing: 20
                        Repeater {
                            model: root.onePaceItems
                            delegate: OnePieceEpisodeCard {
                                required property var modelData
                                entry: modelData
                                sourceLabel: "ONE PACE"
                                onActivated: root.episodeRequested(root.episodeRoute(modelData, "pace"))
                            }
                        }
                    }
                }
            }

            Column {
                id: mangaBlock
                width: parent.width
                spacing: 18
                WidgetHeader {
                    width: parent.width
                    title: "Manga"
                    sub: root.countLabel(root.volumeItems.length, "volume") + " · Tankoban"
                    navigable: false
                }
                Flickable {
                    width: parent.width
                    height: 380
                    contentWidth: mangaRow.width
                    contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: mangaRow
                        y: 36
                        spacing: 20
                        Repeater {
                            model: root.volumeItems
                            delegate: OnePieceVolumeCard {
                                required property var modelData
                                entry: modelData
                                colorEdition: false
                                onActivated: root.mangaVolumeRequested(false, String(modelData.number))
                            }
                        }
                    }
                }
            }

            Column {
                id: colorBlock
                width: parent.width
                spacing: 18
                WidgetHeader {
                    width: parent.width
                    title: "Colored Manga"
                    sub: root.countLabel(root.volumeItems.length, "volume") + " · Tankoban · Colored edition"
                    navigable: false
                }
                Flickable {
                    width: parent.width
                    height: 380
                    contentWidth: colorRow.width
                    contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: colorRow
                        y: 36
                        spacing: 20
                        Repeater {
                            model: root.volumeItems
                            delegate: OnePieceVolumeCard {
                                required property var modelData
                                entry: modelData
                                colorEdition: true
                                onActivated: root.mangaVolumeRequested(true, String(modelData.number))
                            }
                        }
                    }
                }
            }

            Column {
                id: liveBlock
                width: parent.width
                spacing: 18
                WidgetHeader {
                    width: parent.width
                    title: "Live Action"
                    sub: root.liveLoading ? "Cinemeta · loading"
                         : root.countLabel(root.liveItems.length, "episode") + " · Cinemeta"
                    navigable: false
                }
                Flickable {
                    width: parent.width
                    height: 216
                    contentWidth: liveRow.width
                    contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: liveRow
                        spacing: 20
                        Repeater {
                            model: root.liveItems
                            delegate: OnePieceEpisodeCard {
                                required property var modelData
                                entry: modelData
                                sourceLabel: "LIVE ACTION"
                                onActivated: root.episodeRequested(root.episodeRoute(modelData, "live"))
                            }
                        }
                    }
                }
            }

            Column {
                id: relatedBlock
                width: parent.width
                spacing: 18
                visible: root.relatedItems.length > 0
                WidgetHeader {
                    width: parent.width
                    title: "Related Adaptations"
                    sub: root.countLabel(root.relatedItems.length, "title")
                    navigable: false
                }
                Flickable {
                    width: parent.width
                    height: 216
                    contentWidth: relatedRow.width
                    contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: relatedRow
                        spacing: 20
                        Repeater {
                            model: root.relatedItems
                            delegate: OnePieceEpisodeCard {
                                required property var modelData
                                entry: modelData
                                sourceLabel: "RELATED"
                                onActivated: root.relatedRequested(modelData)
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 80 }
        }
    }

    ChromeScrim { z: 16 }
    BackAction {
        id: arcBackAction
        x: theme.margin
        y: root.phoneLayout ? 12 : 28
        z: 20
        height: root.phoneLayout ? 48 : implicitHeight
        onTriggered: root.backRequested()
    }

    Text {
        id: arcChromeTitle
        z: 20
        x: root.phoneLayout ? arcBackAction.x + arcBackAction.width + 10 : (parent.width - width) / 2
        width: root.phoneLayout ? Math.max(0, parent.width - x - theme.margin) : implicitWidth
        y: root.phoneLayout ? 12 : 34
        height: root.phoneLayout ? 48 : implicitHeight
        text: root.arc ? root.arc.title.toUpperCase() + " · " + root.sagaLabel.toUpperCase() : ""
        color: theme.inkDim
        font.family: theme.ui
        font.pixelSize: theme.fontSize(root.phoneLayout ? 9 : 10)
        font.bold: true
        font.letterSpacing: root.phoneLayout ? 1.0 : 2.0
        horizontalAlignment: root.phoneLayout ? Text.AlignLeft : Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.NoWrap
        elide: root.phoneLayout ? Text.ElideRight : Text.ElideNone
    }
}
