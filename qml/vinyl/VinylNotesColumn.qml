import QtQuick
import QtQuick.Controls.Basic
import ".."
import "VinylNotePicker.js" as Picker

Item {
    id: root
    objectName: "vinylNotesColumn"
    required property var pageData
    readonly property int shownLineIndex: hasAutomaticNote ? automaticLineIndex : pageData.activeNoteLine
    // Local supplied notes have no crowdsourced votes or reading-budget ranking.
    readonly property bool localNotes: !!pageData.playingEdition
                                       && pageData.playingEdition.origin === "local"
                                       && hasSuppliedLocalNote(pageData.activeNoteLine)
    readonly property bool autoMode: pageData.autoNotes && pageData.manualNoteLine < 0 && !localNotes
    property var automaticNote: null
    property int automaticLineIndex: -1
    property string automaticKey: ""
    property real automaticStartedMs: 0
    property real automaticUntilMs: 0
    readonly property bool hasAutomaticNote: autoMode && automaticNote !== null
    readonly property bool hasPlaybackClock: typeof pageData.position === "number"
                                             && isFinite(pageData.position) && pageData.position >= 0
    readonly property real playbackMs: hasPlaybackClock ? pageData.position * 1000 : 0
    readonly property var shownNotes: {
        if (autoMode)
            return automaticNote ? [automaticNote] : [];
        return pageData.notes[pageData.activeNoteLine] || [];
    }
    readonly property int noteCount: shownNotes.length
    readonly property bool hasSharedScrollY: pageData && typeof pageData.notesScrollY === "number"
    property real fallbackScrollY: 0
    property bool restoreScheduled: false
    property bool applyingRestore: false
    property bool writingScrollY: false
    property bool inlineAttribution: true
    property bool initialRestoreComplete: false
    property bool externalRestorePending: false

    function clearAutomaticNote() {
        automaticNote=null;automaticLineIndex=-1;automaticKey="";
        automaticStartedMs=0;automaticUntilMs=0;
    }
    function updateAutomaticNote(force) {
        if (!pageData || !autoMode || !visible) {
            clearAutomaticNote();
            return;
        }
        if (!force && hasAutomaticNote && hasPlaybackClock
                && playbackMs >= automaticStartedMs && playbackMs < automaticUntilMs)
            return;
        var line=pageData.activeNoteLine;
        // Native QVariantList sequences need a JS-array boundary for the picker.
        var candidates;
        try { candidates=JSON.parse(JSON.stringify(pageData.notes[line] || [])); }
        catch (error) { clearAutomaticNote();return; }
        var chosen=Picker.pickAuto(candidates,pageData.noteWindow(line));
        if (!chosen) {
            clearAutomaticNote();
            return;
        }
        var key=JSON.stringify([chosen.id || "",chosen.sourceUrl || "",chosen.text]);
        if (!force && automaticLineIndex===line && automaticKey===key)
            return;
        automaticLineIndex=line;automaticKey=key;automaticNote=chosen;
        automaticStartedMs=playbackMs;automaticUntilMs=playbackMs+chosen.autoReadMs;
        scheduleRestoreScrollY();
    }
    function openFullAnnotation() {
        var line=shownLineIndex;
        if (line >= 0 && typeof pageData.showNotes === "function") {
            pageData.showNotes(line);
            notesScroll.forceActiveFocus(Qt.OtherFocusReason);
        }
    }
    onAutoModeChanged: Qt.callLater(function(){root.updateAutomaticNote(true);})
    onPlaybackMsChanged: updateAutomaticNote(false)
    onPageDataChanged: {
        clearAutomaticNote();
        Qt.callLater(function(){root.updateAutomaticNote(true);});
    }

    function geniusAuthorNames(authors) {
        if (!authors || typeof authors.length !== "number")
            return "";
        var names = [];
        for (var i = 0; i < authors.length; ++i) {
            var author = authors[i];
            if (!author || typeof author.name !== "string")
                continue;
            var name = author.name.trim();
            if (name.length)
                names.push(name);
        }
        return names.join(", ");
    }

    function hasSuppliedLocalNote(lineIndex) {
        var list = pageData.notes[lineIndex] || [];
        for (var i = 0; i < list.length; ++i) {
            if (list[i] && list[i].source !== "genius")
                return true;
        }
        return false;
    }

    function safeGeniusUrl(value) {
        if (typeof value !== "string" || !value.length || value.trim() !== value
                || /[\u0000-\u0020\\<>"'`]/.test(value))
            return "";
        return /^https:\/\/(?:www\.)?genius\.com\/[^\s\\?#]+(?:\?[^\s\\#]*)?(?:#[^\s\\]*)?$/i.test(value)
                ? value : "";
    }

    function savedScrollY() {
        var value = hasSharedScrollY ? Number(pageData.notesScrollY) : fallbackScrollY;
        return isFinite(value) ? Math.max(0, value) : 0;
    }

    function saveScrollY(value) {
        var y = Math.max(0, Number(value) || 0);
        if (hasSharedScrollY) {
            writingScrollY = true;
            pageData.notesScrollY = y;
            writingScrollY = false;
        } else
            fallbackScrollY = y;
    }

    function restoreScrollY() {
        restoreScheduled = false;
        if (notesScroll.height <= 0)
            return;
        applyingRestore = true;
        notesScroll.contentY = Math.max(0, Math.min(
            notesScroll.contentHeight - notesScroll.height, savedScrollY()));
        applyingRestore = false;
        initialRestoreComplete = true;
        externalRestorePending = false;
    }

    function scheduleRestoreScrollY(externalValueChanged) {
        if (externalValueChanged)
            externalRestorePending = true;
        if (restoreScheduled)
            return;
        restoreScheduled = true;
        Qt.callLater(function() { root.restoreScrollY(); });
    }

    readonly property string annotationFontFamily: notesFace.name || theme.display
    clip: true
    FontLoader { id: notesFace; source: Qt.resolvedUrl("../../assets/fonts/Literata-Regular.ttf") }
    Flickable {
        id: notesScroll
        objectName: "vinylNotesScroll"
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: Math.max(height, notesContent.height)
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true
        ScrollGlide { flick: notesScroll }

        function moveTo(y) {
            var maxY = Math.max(0, contentHeight - height);
            contentY = Math.max(0, Math.min(maxY, y));
        }

        Keys.onPressed: event => {
            if (event.key === Qt.Key_Home)
                moveTo(0);
            else if (event.key === Qt.Key_End)
                moveTo(contentHeight);
            else if (event.key === Qt.Key_PageUp)
                moveTo(contentY - height * 0.85);
            else if (event.key === Qt.Key_PageDown)
                moveTo(contentY + height * 0.85);
            else
                return;
            event.accepted = true;
        }

        onContentHeightChanged: root.scheduleRestoreScrollY()
        onContentWidthChanged: root.scheduleRestoreScrollY()
        onHeightChanged: root.scheduleRestoreScrollY()
        onContentYChanged: {
            if (root.visible && root.initialRestoreComplete && !root.applyingRestore
                    && !root.externalRestorePending)
                root.saveScrollY(notesScroll.contentY);
        }

        Theme { id: theme }
        Column {
            id: notesContent
            objectName: "vinylNotesContent"
            width: notesScroll.width
            height: implicitHeight
            spacing: 20
            y: root.autoMode && height < notesScroll.height
               ? Math.max(0, (notesScroll.height - height) / 2) : 0
            Text {
                visible: !root.autoMode
                text: "ANNOTATIONS · " + root.noteCount
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 11
                font.letterSpacing: 1.5
            }
            Repeater {
                model: root.shownNotes
                delegate: Column {
                    id: noteDelegate
                    required property var modelData
                    required property int index
                    width: notesContent.width
                    height: implicitHeight
                    spacing: 18
                    Rectangle {
                        width: parent.width
                        height: 1
                        color: theme.edge
                        visible: parent.index > 0
                    }
                    Text {
                        id: annotationBody
                        objectName: "vinylAnnotationBody_" + noteDelegate.index
                        width: parent.width
                        text: parent.modelData.text
                        textFormat: Text.PlainText
                        color: theme.ink
                        font.family: root.annotationFontFamily
                        font.pixelSize: root.autoMode ? 24 : 19
                        wrapMode: Text.WordWrap
                        lineHeightMode: Text.FixedHeight
                        lineHeight: font.pixelSize * 1.76
                        KeyboardAction {
                            objectName: "vinylAutoAnnotationOpen_" + noteDelegate.index
                            anchors.fill: parent
                            visible: root.autoMode
                            enabled: root.autoMode && typeof root.pageData.showNotes === "function"
                            accessibleName: qsTr("Read full annotation")
                            onTriggered: root.openFullAnnotation()
                        }
                    }
                    Column {
                        id: geniusAttribution
                        objectName: "vinylGeniusAttribution"
                        readonly property bool geniusNote: !!noteDelegate.modelData
                                                            && noteDelegate.modelData.source === "genius"
                        readonly property string authorNames: root.geniusAuthorNames(
                            noteDelegate.modelData ? noteDelegate.modelData.authors : null)
                        readonly property string sourceUrl: root.safeGeniusUrl(
                            noteDelegate.modelData ? noteDelegate.modelData.sourceUrl : "")
                        visible: root.inlineAttribution && geniusNote && (authorNames.length > 0 || sourceUrl.length > 0)
                        width: noteDelegate.width
                        spacing: 4
                        Text {
                            objectName: "vinylGeniusAuthors"
                            visible: text.length > 0
                            width: parent.width
                            text: geniusAttribution.authorNames
                            textFormat: Text.PlainText
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                        Button {
                            id: geniusLink
                            objectName: "vinylGeniusLink"
                            text: "View annotation on Genius"
                            visible: geniusAttribution.sourceUrl.length > 0
                            enabled: visible
                            activeFocusOnTab: true
                            padding: 0
                            flat: true
                            Accessible.role: Accessible.Link
                            Accessible.name: text
                            background: Item {}
                            contentItem: Text {
                                text: geniusLink.text
                                textFormat: Text.PlainText
                                color: geniusLink.activeFocus ? theme.gold : theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 12
                                font.underline: true
                            }
                            onClicked: {
                                if (geniusAttribution.sourceUrl.length > 0)
                                    Qt.openUrlExternally(geniusAttribution.sourceUrl);
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: root.pageData
        ignoreUnknownSignals: true
        function onNotesScrollYChanged() {
            if (!root.writingScrollY
                    && Math.abs(root.savedScrollY() - notesScroll.contentY) > 0.5)
                root.scheduleRestoreScrollY(true);
        }
        function onActiveNoteLineChanged() { root.updateAutomaticNote(false); }
        function onNotesChanged() { root.updateAutomaticNote(true); }
        function onPlaybackDiscontinuity() {
            root.clearAutomaticNote();
            Qt.callLater(function(){root.updateAutomaticNote(true);});
        }
        function onReadingContextKeyChanged() {
            root.clearAutomaticNote();
            Qt.callLater(function(){root.updateAutomaticNote(true);});
        }
        function onLyricsDocumentIdChanged() {
            root.clearAutomaticNote();
            Qt.callLater(function(){root.updateAutomaticNote(true);});
        }
    }

    onVisibleChanged: {
        if (visible) scheduleRestoreScrollY();
        Qt.callLater(function(){root.updateAutomaticNote(true);});
    }
    onHeightChanged: scheduleRestoreScrollY()
    Component.onCompleted: {
        scheduleRestoreScrollY();
        Qt.callLater(function(){root.updateAutomaticNote(true);});
    }
}
