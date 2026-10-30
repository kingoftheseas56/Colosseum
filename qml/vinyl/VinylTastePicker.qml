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
    readonly property bool editing: worldData.onboardingState === "editing"
    readonly property int minimumArtists: worldData.recommendationService
        ? Number(worldData.recommendationService.minimumArtists) || 5 : 5
    readonly property int selectedArtistCount: worldData.usesRecommendationService
        ? Number(worldData.recommendationService.draftArtistCount) || 0 : selected.length
    implicitHeight: pageFlick ? pageFlick.height : header.height + 30 + grid.implicitHeight
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
            artistScroll.contentY = Math.max(0, Math.min(
                Math.max(0, artistScroll.contentHeight - artistScroll.height), card.y));
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
            text: "What do you listen to?"
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
                visible: root.editing
                enabled: visible
                label: "Cancel taste editing"
                accessibleName: label
                symbol: "cancel"
                backdrop: root.backdrop
                onTriggered: root.worldData.cancelTasteEdit()
            }
            VinylWorldButton {
                objectName: "vinylPickerDone"
                enabled: root.selectedArtistCount >= root.minimumArtists
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
                    : root.selectedArtistCount >= root.minimumArtists
                        ? qsTr("%1 artists selected.").arg(root.selectedArtistCount)
                        : qsTr("%1 of %2 artists selected.").arg(root.selectedArtistCount).arg(root.minimumArtists)
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
                    required property var modelData
                    required property int index
                    objectName: "vinylPickerArtist_" + index
                    width: (root.width - (grid.columns - 1) * 24) / grid.columns
                    height: width + label.implicitHeight + 18
                    accessibleName: modelData.title
                    onTriggered: root.toggleArtist(modelData)
                    onActiveFocusChanged: if (activeFocus) root.currentIndex = index
                    Keys.onPressed: event => {
                        if ([Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End].indexOf(event.key) >= 0)
                            nav.handle(event);
                    }

                    RoundedPosterImage {
                        width: parent.width
                        height: width
                        radius: width / 2
                        sources: [root.worldData.artwork(modelData.cover)]
                        hovered: root.selected.indexOf(modelData.key) >= 0
                    }
                    Text {
                        id: label
                        y: parent.width + 12
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: parent.modelData.title
                        wrapMode: Text.WordWrap
                        color: root.selected.indexOf(parent.modelData.key) >= 0 ? theme.gold : theme.ink
                        font.family: theme.ui
                        font.pixelSize: 14
                    }
                }
            }
        }
    }
}
