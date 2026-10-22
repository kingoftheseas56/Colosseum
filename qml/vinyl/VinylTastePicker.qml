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
    implicitHeight: pageFlick ? pageFlick.height : header.height + 30 + grid.implicitHeight
    Theme { id: theme }
    property int currentIndex: 0
    Keys.onPressed: event => nav.handle(event)

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

        Row {
            id: actions
            anchors.right: parent.right
            y: 62
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
                objectName: "vinylPickerSkip"
                visible: !root.editing
                enabled: visible
                label: "Skip artist onboarding"
                accessibleName: label
                symbol: "playNext"
                backdrop: root.backdrop
                onTriggered: root.worldData.skipPicker()
            }
            VinylWorldButton {
                objectName: "vinylPickerDone"
                label: "Done with artist selection"
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
                    ? (root.query ? "No artists found." : "No artist suggestions yet.")
                    : root.selected.length + (root.selected.length === 1 ? " artist selected." : " artists selected.")
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
