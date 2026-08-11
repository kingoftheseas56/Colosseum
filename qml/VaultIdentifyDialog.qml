// VaultIdentifyDialog — the small explicit identity gesture. The native VaultIdentifier remains
// the certainty gate; this surface is deliberately honest when no single offline candidate exists.
import QtQuick
import QtQuick.Controls
import "TheatreApi.js" as TheatreApi

Popup {
    id: dialog
    objectName: "vaultIdentifyDialog"
    modal: true
    focus: true
    width: 590
    height: 520
    property string groupKey: ""
    property string titleText: ""
    property string kind: ""
    property string feedback: ""
    property string query: ""
    property var results: []
    property var searchProvider: null      // deterministic fixture seam; production uses context catalogues
    property var embeddedIdentity: ({})
    property int searchGeneration: 0
    signal identityChosen(string groupKey, var identity)

    function candidate(source, row) {
        row = row || {}
        var id = row.sourceId || row.id || row.tt || row.imdb_id || row.mal_id || row.gcdId || ""
        var title = row.title || row.name || row.title_english || "Untitled"
        if (source === "MAL") title = row.title_english || row.title || title
        var year = Number(row.year || row.releaseYear || 0)
        if (!year && row.releaseInfo) {
            var m = String(row.releaseInfo).match(/(19|20)\d{2}/)
            if (m) year = Number(m[0])
        }
        var sourceId = String(id)
        if (sourceId && sourceId.indexOf(":") < 0)
            sourceId = source.toLowerCase() + ":" + sourceId
        return {
            source: source,
            sourceId: sourceId,
            title: String(title),
            year: year,
            synopsis: String(row.synopsis || row.description || row.overview || ""),
            coverUrl: String(row.coverUrl || row.cover || row.poster ||
                             (source === "IMDB" && id ? "https://live.metahub.space/poster/small/" + id + "/img" : "")),
            world: source === "IMDB" ? "Theatre" : source === "MAL" ? "Tankoban" :
                   source === "EPUB" ? "Biblio" : "Tankoban",
            detail: String(row.publisher || row.type || row.medium || source)
        }
    }

    function setRows(rows, source) {
        var out = []
        rows = rows || []
        for (var i = 0; i < rows.length; i++) {
            var row = rows[i]
            if (row && row.source && !source) out.push(row)
            else out.push(candidate(source, row))
        }
        dialog.results = out
        dialog.feedback = out.length ? "Choose the identity that matches this folder. Your files stay in place."
                                       : "No catalogue candidates yet. Try a shorter title."
    }

    function searchNow() {
        var text = String(dialog.query || "").trim()
        var generation = ++dialog.searchGeneration
        if (!text) { dialog.results = []; dialog.feedback = "Type a title to search."; return }
        var provider = dialog.searchProvider
        if (provider && provider.search) {
            var injected = provider.search(text, dialog.kind, function(rows) {
                if (generation === dialog.searchGeneration) setRows(rows)
            })
            if (injected && injected.length !== undefined && generation === dialog.searchGeneration)
                setRows(injected)
            return
        }
        if (dialog.kind === "book") {
            var embedded = dialog.embeddedIdentity || {}
            if (embedded.title) setRows([embedded], "EPUB")
            else setRows([], "EPUB")
            return
        }
        if (dialog.kind === "video") {
            var imdbRows = (typeof ImdbCatalog !== "undefined" && ImdbCatalog.ready())
                ? ImdbCatalog.search(text, 20) : []
            if (imdbRows.length) { setRows(imdbRows, "IMDB"); return }
            // Network is deliberately reachable only from this explicit video-miss branch.
            TheatreApi.searchTitle(text, function(metas) {
                if (generation === dialog.searchGeneration) setRows(metas, "IMDB")
            })
            return
        }
        var comicRows = (typeof ComicsCatalog !== "undefined" && ComicsCatalog.ready())
            ? ComicsCatalog.search(text, 20) : []
        if (comicRows.length) { setRows(comicRows, "COMICS"); return }
        var malRows = (typeof MalCatalog !== "undefined" && MalCatalog.ready())
            ? MalCatalog.search(text, 20, "") : []
        setRows(malRows, "MAL")
    }

    onOpened: {
        dialog.query = dialog.titleText
        dialog.searchNow()
        queryField.forceActiveFocus()
        queryField.selectAll()
    }

    background: Rectangle {
        radius: 16
        color: Qt.rgba(0.055, 0.065, 0.09, 0.98)
        border.width: 1
        border.color: theme.edge
    }
    Theme { id: theme }

    contentItem: Column {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 10
        Text {
            text: "Identify this Vault folder"
            color: theme.ink; font.family: theme.display; font.pixelSize: 24
        }
        Text {
            width: parent.width
            text: "Folder: " + (dialog.titleText || "Untitled folder")
            color: theme.gold; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: dialog.feedback || "Search offline catalogues first. Your files stay in place."
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12; wrapMode: Text.WordWrap
        }
        TextField {
            id: queryField
            objectName: "vaultIdentifyQuery"
            width: parent.width
            placeholderText: "Type a title"
            text: dialog.query
            selectByMouse: true
            onTextChanged: {
                if (dialog.query !== text) dialog.query = text
                if (dialog.visible) dialog.searchNow()
            }
        }
        ListView {
            id: resultList
            objectName: "vaultIdentifyResults"
            width: parent.width
            height: 315
            clip: true
            spacing: 6
            model: dialog.results
            delegate: Rectangle {
                objectName: "vaultIdentifyRow_" + index
                width: resultList.width
                height: 58
                radius: 9
                color: Qt.rgba(1, 1, 1, 0.05)
                border.width: 1; border.color: theme.edge
                Image {
                    x: 8; y: 7; width: 44; height: 44
                    source: modelData.coverUrl || ""
                    fillMode: Image.PreserveAspectCrop
                    visible: source.length > 0
                    asynchronous: true
                }
                Column {
                    x: 62; y: 8; width: parent.width - 172; spacing: 2
                    Text {
                        width: parent.width
                        text: modelData.title || "Untitled"
                        color: theme.ink; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: (modelData.year ? modelData.year + " · " : "") + (modelData.source || "") +
                              (modelData.detail ? " · " + modelData.detail : "")
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }
                Rectangle {
                    objectName: "vaultIdentifyUse_" + index
                    anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                    width: useText.implicitWidth + 18; height: 32; radius: 8; color: theme.gold
                    Text { id: useText; anchors.centerIn: parent; text: "Use this"; color: "#151310"; font.pixelSize: 12; font.weight: Font.DemiBold }
                    MouseArea { anchors.fill: parent; onClicked: dialog.identityChosen(dialog.groupKey, modelData) }
                }
            }
        }
        Item { width: 1; height: 1 }
        Row {
            spacing: 10
            anchors.right: parent.right
            Rectangle {
                objectName: "vaultIdentifyCancel"
                width: cancelText.implicitWidth + 28; height: 36; radius: 9
                color: Qt.rgba(1, 1, 1, 0.06); border.width: 1; border.color: theme.edge
                Text { id: cancelText; anchors.centerIn: parent; text: "Cancel"; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13 }
                MouseArea { anchors.fill: parent; onClicked: dialog.close() }
            }
        }
    }
}
