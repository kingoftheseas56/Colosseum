import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    objectName: "vinylTastePicker"
    required property var worldData
    required property Item backdrop
    required property Flickable pageFlick
    readonly property string query: worldData.pickerQuery
    readonly property var selected: worldData.pickerDraft
    readonly property var offered: worldData.artistOptions(query)
    readonly property int selectedArtistCount: worldData.usesRecommendationService
        ? Number(worldData.recommendationService.draftArtistCount) || 0 : selected.length
    implicitHeight: header.height + 30 + grid.implicitHeight
    Theme { id: theme }
    property int currentIndex: 0
    Keys.onPressed: event => { if (!artistSearch.activeFocus) nav.handle(event) }

    KeyboardCollectionController {
        id: nav
        view: root
        count: root.offered.length
        orientation: "grid"
        columns: grid.columns
        positionIndexFn: function(index) {
            var card = artistCards.itemAt(index);
            if (!card) return;
            if (root.pageFlick) {
                var at = card.mapToItem(root.pageFlick.contentItem, 0, 0);
                var top = root.pageFlick.contentY;
                var nextY = at.y < top + 12 ? at.y - 12
                    : at.y + card.height > top + root.pageFlick.height
                        ? at.y + card.height - root.pageFlick.height : top;
                root.pageFlick.contentY = Math.max(0, Math.min(
                    root.pageFlick.contentHeight - root.pageFlick.height, nextY));
            }
            Qt.callLater(function() {
                var next = artistCards.itemAt(index);
                if (next) next.forceActiveFocus();
            });
        }
        onActivated: index => root.toggleArtist(root.offered[index])
    }

    function toggleArtist(artist) { worldData.togglePickerArtist(artist); }
    function setArtistSearchQuery(value) {
        if (typeof worldData.setArtistSearchQuery === "function")
            return worldData.setArtistSearchQuery(value);
        if (typeof worldData.setPickerQuery === "function")
            return worldData.setPickerQuery(value);
    }
    function submitArtistSearch() {
        if (typeof worldData.submitArtistSearch === "function")
            return worldData.submitArtistSearch();
    }
    function focusArtistSearch() {
        if (!artistSearch.visible || !artistSearch.enabled)
            return false;
        artistSearch.forceActiveFocus(Qt.MouseFocusReason);
        return true;
    }

    Item {
        id: header
        width: parent.width
        height: 140

        Text {
            objectName: "vinylPickerQuestion"
            text: qsTr("Pick a few artists you love")
            width: Math.max(0, parent.width - actions.width - 24)
            elide: Text.ElideRight
            fontSizeMode: Text.Fit
            minimumPixelSize: 22
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 40
        }

        TextField {
            id: artistSearch
            objectName: "vinylPickerArtistSearch"
            x: 0
            y: 54
            width: Math.max(0, parent.width - actions.width - 12)
            height: 44
            text: root.query
            color: theme.ink
            placeholderTextColor: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 14
            padding: 14
            leftPadding: 42
            selectByMouse: true
            placeholderText: qsTr("Search artists")
            Accessible.name: qsTr("Search artists")
            onTextEdited: root.setArtistSearchQuery(text)
            onAccepted: root.submitArtistSearch()
            background: Glass {
                objectName: "vinylPickerArtistSearchGlass"
                backdrop: root.backdrop
                radius: 12
                tint: 0.06
                scrim: 0.18
            }
            VinylControlIcon {
                x: 14
                anchors.verticalCenter: parent.verticalCenter
                symbol: "search"
                color: theme.inkDimmer
                Accessible.ignored: true
            }
            FocusRing {
                anchors.fill: parent
                shown: artistSearch.activeFocus && [Qt.TabFocusReason, Qt.BacktabFocusReason,
                    Qt.ShortcutFocusReason].indexOf(artistSearch.focusReason) >= 0
                radius: 12
            }
        }

        Row {
            id: actions
            anchors.right: parent.right
            anchors.verticalCenter: artistSearch.verticalCenter
            spacing: 8

            VinylWorldButton {
                objectName: "vinylPickerCancel"
                label: qsTr("Cancel taste editing")
                accessibleName: label
                symbol: "cancel"
                backdrop: root.backdrop
                onTriggered: root.worldData.cancelTasteEdit()
            }
            VinylWorldButton {
                objectName: "vinylPickerDone"
                enabled: root.selectedArtistCount > 0
                label: qsTr("Save artist choices")
                accessibleName: label
                symbol: "check"
                selected: true
                backdrop: root.backdrop
                onTriggered: root.worldData.answerPicker()
            }
        }
    }

    Text {
        objectName: "vinylPickerStatus"
        y: 112
        width: Math.max(0, parent.width - 96)
        text: root.worldData.artistSearchError
            ? root.worldData.artistSearchError
            : root.worldData.artistSearchLoading
                ? "Searching artists…"
                : root.offered.length === 0
                    ? (root.query ? qsTr("No artists found.") : qsTr("Search for artists or choose from your library."))
                    : root.selectedArtistCount === 1 ? qsTr("1 artist selected.")
                        : qsTr("%1 artists selected.").arg(root.selectedArtistCount)
        color: root.worldData.artistSearchError ? theme.ink : theme.inkDim
        font.family: theme.ui
        font.pixelSize: 12
        elide: Text.ElideRight
    }

    Row {
        y: 100
        anchors.right: parent.right
        spacing: 8

        VinylWorldButton {
            objectName: "vinylPickerRetry"
            visible: !!root.worldData.artistSearchError
            enabled: visible
            label: "Retry artist search"
            accessibleName: label
            symbol: "retry"
            backdrop: root.backdrop
            onTriggered: root.worldData.retryArtistSearch()
        }
        VinylWorldButton {
            objectName: "vinylPickerLoadMore"
            visible: root.worldData.artistSearchHasMore
            enabled: visible && !root.worldData.artistSearchLoading
            label: "Load more artists"
            accessibleName: label
            symbol: "arrowDown"
            backdrop: root.backdrop
            onTriggered: root.worldData.loadMoreArtistSearch()
        }
    }

    Flickable {
        id: artistScroll
        objectName: "vinylPickerScroll"
        y: header.height + 30
        width: parent.width
        height: Math.max(0, parent.height - y)
        contentWidth: width
        contentHeight: grid.implicitHeight
        interactive: false
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Grid {
            id: grid
            columns: Math.max(2, Math.min(7, Math.floor((root.width + 24) / 150)))
            columnSpacing: 24
            rowSpacing: 28

            Repeater {
                id: artistCards
                model: root.offered
                delegate: KeyboardAction {
                    id: artistCard
                    required property var modelData
                    required property int index
                    objectName: "vinylPickerArtist_" + index
                    width: (root.width - (grid.columns - 1) * 24) / grid.columns
                    height: artistCaption.y + artistCaption.height + 8
                    readonly property bool chosen: root.selected.indexOf(modelData.key) >= 0
                    showFocusFrame: false
                    focusRadius: width * 0.1
                    Accessible.role: Accessible.CheckBox
                    Accessible.checkable: true
                    Accessible.checked: chosen
                    accessibleName: modelData.title
                    onTriggered: root.toggleArtist(modelData)
                    onActiveFocusChanged: if (activeFocus) root.currentIndex = index
                    Keys.onPressed: event => {
                        if ([Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End].indexOf(event.key) >= 0)
                            nav.handle(event);
                    }

                    VinylArtistWidget {
                        id: artwork
                        objectName: "vinylPickerArtistArtwork_" + artistCard.index
                        width: parent.width
                        height: implicitHeight
                        artworkSource: root.worldData.artwork(artistCard.modelData.cover)
                        title: artistCard.modelData.title
                        displayFont: theme.display
                        Accessible.ignored: true
                    }
                    Text {
                        id: artistCaption
                        objectName: "vinylPickerArtistCaption_" + artistCard.index
                        y: artwork.height + 8
                        width: parent.width
                        height: 20
                        text: artistCard.modelData.title
                        textFormat: Text.PlainText
                        color: theme.ink
                        font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }
                    Image {
                        objectName: "vinylPickerArtistSelected_" + artistCard.index
                        visible: artistCard.chosen
                        x: parent.width - width - 9
                        y: 9
                        width: 18; height: 18
                        source: "../../assets/icons/lucide/circle-check.svg"
                        sourceSize.width: 36; sourceSize.height: 36
                        Accessible.ignored: true
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: artistCaption.y + artistCaption.height + 4
                        width: parent.width * 0.3
                        height: 2; radius: 1
                        visible: artistCard.chosen || artistCard.activeFocus
                        color: theme.gold
                        opacity: artistCard.chosen ? 0.7 : 0.4
                        Accessible.ignored: true
                    }
                }
            }
        }
    }
}
