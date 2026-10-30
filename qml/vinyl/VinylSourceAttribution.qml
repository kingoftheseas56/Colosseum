import QtQuick
import QtQuick.Controls.Basic
import ".."

Item {
    id: root
    objectName: "vinylSourceAttributionRail"

    property string lyricsSourceUrl: ""
    property string annotationSourceUrl: ""
    property var annotationSources: []
    property var contributors: []
    property string contextKey: ""
    property alias sourcesPopupControl: sourcesPopup
    property alias contributorsPopupControl: contributorsPopup

    property Item pendingFocusReturn: null
    property bool suppressFocusReturn: false

    signal sourceRequested(string url)

    readonly property string validatedLyricsSourceUrl: safeLyricsSource(lyricsSourceUrl)
    readonly property string validatedAnnotationSourceUrl: safeGeniusUrl(annotationSourceUrl)
    readonly property var validatedAnnotationSources: {
        var input = root.listInput(root.annotationSources);
        var result = [];
        var seen = Object.create(null);
        for (var i = 0; i < input.length; ++i) {
            var url = root.safeGeniusUrl(input[i]);
            if (!url)
                continue;
            var key = root.urlKey(url);
            if (seen[key])
                continue;
            seen[key] = true;
            result.push(url);
        }
        if (result.length === 0 && root.validatedAnnotationSourceUrl)
            result.push(root.validatedAnnotationSourceUrl);
        return result;
    }
    readonly property var sourceEntries: {
        var result = [];
        if (root.validatedLyricsSourceUrl)
            result.push({ label: "LRCLIB lyrics", kind: "lyrics", url: root.validatedLyricsSourceUrl, icon: "../../assets/icons/providers/lrclib.ico" });
        for (var i = 0; i < root.validatedAnnotationSources.length; ++i) {
            result.push({ label: "Annotation " + (i + 1), kind: "annotation", url: root.validatedAnnotationSources[i], icon: "../../assets/icons/providers/genius.png" });
        }
        return result;
    }
    readonly property var displayedContributors: {
        var input = root.listInput(root.contributors);
        var result = [];
        for (var i = 0; i < input.length; ++i) {
            var contributor = input[i];
            if (!contributor || typeof contributor !== "object"
                    || typeof contributor.name !== "string" || !contributor.name.trim())
                continue;
            result.push({
                name: contributor.name,
                id: typeof contributor.id === "string" ? contributor.id : "",
                profileUrl: root.safeGeniusUrl(contributor.url)
            });
        }
        return result;
    }

    readonly property Item popupHost: {
        var overlay = Overlay.overlay;
        if (overlay)
            return overlay;
        var window = root.Window.window;
        if (window && window.contentItem)
            return window.contentItem;
        return root.parent || root;
    }
    readonly property real popupViewportWidth: root.Window.window
        ? root.Window.window.width : (root.popupHost ? root.popupHost.width : root.width)
    readonly property real popupViewportHeight: root.Window.window
        ? root.Window.window.height : (root.popupHost ? root.popupHost.height : root.height)

    implicitWidth: actions.implicitWidth
    implicitHeight: actions.implicitHeight

    function hasForbiddenUrlCharacters(value) {
        if (typeof value !== "string" || value.length === 0
                || /[\s\u0000-\u001f\u007f\\]/.test(value)
                || /%(?![0-9a-f]{2})/i.test(value))
            return true;
        try {
            if (/[\s\u0000-\u001f\u007f-\u009f\\]/.test(decodeURIComponent(value)))
                return true;
        } catch (error) {
            return true;
        }
        return false;
    }

    function safeLyricsSource(value) {
        if (hasForbiddenUrlCharacters(value))
            return "";
        return /^https:\/\/lrclib\.net(?:[/?#][^\\]*)?$/i.test(value) ? value : "";
    }

    function listInput(value) {
        return value && typeof value.length === "number" ? value : [];
    }

    function safeGeniusUrl(value) {
        if (hasForbiddenUrlCharacters(value))
            return "";
        return /^https:\/\/(?:www\.)?genius\.com(?:[/?#][^\\]*)?$/i.test(value) ? value : "";
    }

    function safeExternalUrl(value) {
        return safeLyricsSource(value) || safeGeniusUrl(value);
    }

    function urlKey(url) {
        var match = /^(https):\/\/([^/?#]+)(.*)$/i.exec(url);
        return match ? "https://" + match[2].toLowerCase() + match[3] : url;
    }

    function requestLyricsSource(url) {
        var safeUrl = safeLyricsSource(url);
        if (!safeUrl)
            return false;
        sourceRequested(safeUrl);
        return true;
    }

    function requestAnnotationSource(url) {
        var safeUrl = safeGeniusUrl(url);
        if (!safeUrl)
            return false;
        sourceRequested(safeUrl);
        return true;
    }

    function requestContributorProfile(url) {
        return requestAnnotationSource(url);
    }

    onSourceRequested: function(url) {
        var safeUrl = safeExternalUrl(url);
        if (safeUrl)
            Qt.openUrlExternally(safeUrl);
    }

    function positionPopup(popup) {
        var host = popup.parent;
        if (!host)
            return;
        var topLeft = root.mapToItem(host, 0, 0);
        var bottomLeft = root.mapToItem(host, 0, root.height);
        var maxX = Math.max(0, root.popupViewportWidth - popup.width);
        var marginX = Math.min(8, maxX / 2);
        popup.x = Math.max(marginX, Math.min(maxX - marginX,
            topLeft.x + root.width / 2 - popup.width / 2));
        var maxY = Math.max(0, root.popupViewportHeight - popup.height);
        var marginY = Math.min(8, maxY / 2);
        var desiredY = bottomLeft.y + 8;
        if (desiredY + popup.height > root.popupViewportHeight - marginY)
            desiredY = topLeft.y - popup.height - 8;
        popup.y = Math.max(marginY, Math.min(maxY - marginY, desiredY));
    }

    function handleScrollKey(view, event) {
        var amount;
        if (event.key === Qt.Key_PageDown)
            amount = view.height * 0.85;
        else if (event.key === Qt.Key_PageUp)
            amount = -view.height * 0.85;
        else if (event.key === Qt.Key_Home)
            amount = -view.contentHeight;
        else if (event.key === Qt.Key_End)
            amount = view.contentHeight;
        else
            return;
        view.cancelFlick();
        view.contentY = Math.max(0, Math.min(view.contentHeight - view.height,
                                             view.contentY + amount));
        event.accepted = true;
    }

    function dismissPopupsWithoutFocus() {
        suppressFocusReturn = true;
        if (sourcesPopup.visible)
            sourcesPopup.close();
        if (contributorsPopup.visible)
            contributorsPopup.close();
        pendingFocusReturn = null;
        suppressFocusReturn = false;
    }

    function restoreFocusFromPopup() {
        var invoker = pendingFocusReturn;
        pendingFocusReturn = null;
        if (suppressFocusReturn || !invoker)
            return;
        Qt.callLater(function() {
            var window = root.Window.window;
            var focused = window ? window.activeFocusItem : null;
            if (focused && focused !== invoker && focused !== window.contentItem)
                return;
            if (root.visible && invoker.visible)
                invoker.forceActiveFocus(Qt.PopupFocusReason);
        });
    }

    function repositionOpenPopups() {
        if (sourcesPopup && sourcesPopup.visible)
            positionPopup(sourcesPopup);
        if (contributorsPopup && contributorsPopup.visible)
            positionPopup(contributorsPopup);
    }

    function openPopup(popup, invoker) {
        if (popup.opened) {
            popup.close();
            return;
        }
        dismissPopupsWithoutFocus();
        pendingFocusReturn = invoker;
        positionPopup(popup);
        popup.open();
    }

    onContextKeyChanged: dismissPopupsWithoutFocus()
    onLyricsSourceUrlChanged: dismissPopupsWithoutFocus()
    onAnnotationSourceUrlChanged: dismissPopupsWithoutFocus()
    onAnnotationSourcesChanged: dismissPopupsWithoutFocus()
    onContributorsChanged: dismissPopupsWithoutFocus()
    onVisibleChanged: if (!visible) dismissPopupsWithoutFocus()
    onPopupViewportWidthChanged: Qt.callLater(repositionOpenPopups)
    onPopupViewportHeightChanged: Qt.callLater(repositionOpenPopups)

    Theme { id: theme }

    component AttributionAction: KeyboardAction {
        id: action
        property url iconSource: ""
        property string symbol: ""
        property color symbolColor: "white"
        property bool isLink: false
        width: visible ? 32 : 0
        height: 32
        focusRadius: 16
        Accessible.role: isLink ? Accessible.Link : Accessible.Button
        ToolTip.text: accessibleName
        ToolTip.visible: hovered
        Rectangle {
            anchors.fill: parent
            radius: 16
            color: action.interactionActive ? Qt.rgba(1, 1, 1, 0.14) : "transparent"
        }
        Image {
            objectName: action.objectName + "Image"
            visible: action.iconSource.toString().length > 0
            anchors.centerIn: parent
            width: 18
            height: 18
            source: action.iconSource
            sourceSize.width: 32
            sourceSize.height: 32
            fillMode: Image.PreserveAspectFit
            smooth: true
        }
        VinylControlIcon {
            visible: action.symbol.length > 0
            anchors.centerIn: parent
            width: 18
            height: 18
            symbol: action.symbol
            color: action.symbolColor
        }
    }

    Row {
        id: actions
        objectName: "vinylSourceAttributionActions"
        anchors.fill: parent
        spacing: 4

        AttributionAction {
            id: lyricsAction
            objectName: "vinylSourceAttributionLrclibAction"
            visible: root.validatedLyricsSourceUrl.length > 0
            accessibleName: "Open lyrics source on LRCLIB"
            isLink: true
            iconSource: "../../assets/icons/providers/lrclib.ico"
            onTriggered: root.requestLyricsSource(root.lyricsSourceUrl)
        }

        AttributionAction {
            id: geniusAction
            objectName: "vinylSourceAttributionGeniusAction"
            visible: root.validatedAnnotationSourceUrl.length > 0
            accessibleName: "Open annotation source on Genius"
            isLink: true
            iconSource: "../../assets/icons/providers/genius.png"
            onTriggered: root.requestAnnotationSource(root.annotationSourceUrl)
        }

        AttributionAction {
            id: sourcesAction
            objectName: "vinylSourceAttributionSourcesAction"
            visible: root.sourceEntries.length > 0
            accessibleName: "Show music sources"
            iconSource: "../../assets/icons/lucide/info.svg"
            onTriggered: root.openPopup(sourcesPopup, sourcesAction)
        }

        AttributionAction {
            id: contributorsAction
            objectName: "vinylSourceAttributionContributorsAction"
            visible: root.displayedContributors.length > 0
            accessibleName: "Show annotation contributors"
            onTriggered: root.openPopup(contributorsPopup, contributorsAction)
            symbol: "credits"
            symbolColor: theme.inkDim
        }
    }

    Popup {
        id: sourcesPopup
        objectName: "vinylSourceAttributionSourcesPopup"
        parent: root.popupHost
        width: Math.max(0, Math.min(320, root.popupViewportWidth - 16))
        height: Math.max(0, Math.min(270, root.popupViewportHeight - 16))
        padding: 14
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 14
            color: "#171a22"
            border.width: 1
            border.color: theme.edge
        }
        onOpened: sourceScroll.forceActiveFocus(Qt.PopupFocusReason)
        onClosed: root.restoreFocusFromPopup()
        contentItem: Column {
            spacing: 10
            Text {
                id: sourcesTitle
                objectName: "vinylSourceAttributionSourcesTitle"
                text: "SOURCES"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 11
                font.letterSpacing: 1.4
            }
            ListView {
                id: sourceScroll
                objectName: "vinylSourceAttributionSourcesScroll"
                width: parent.width
                height: Math.max(0, parent.height - sourcesTitle.implicitHeight - 10)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                focus: true
                activeFocusOnTab: true
                Accessible.role: Accessible.Pane
                Accessible.name: "Music source links"
                model: root.sourceEntries
                spacing: 6
                Keys.onPressed: event => root.handleScrollKey(sourceScroll, event)
                delegate: KeyboardAction {
                    id: sourceLink
                    required property int index
                    required property var modelData
                    objectName: "vinylSourceAttributionSourceLink_" + index
                    width: sourceScroll.width
                    height: 36
                    focusRadius: 8
                    accessibleName: "Open " + modelData.label
                    Accessible.role: Accessible.Link
                    ToolTip.text: accessibleName
                    ToolTip.visible: hovered
                    onTriggered: {
                        if (modelData.kind === "lyrics")
                            root.requestLyricsSource(modelData.url);
                        else
                            root.requestAnnotationSource(modelData.url);
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: 8
                        color: sourceLink.interactionActive ? theme.glassHi : Qt.rgba(1, 1, 1, 0.035)
                    }
                    Image {
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 18
                        height: 18
                        source: sourceLink.modelData.icon
                        sourceSize.width: 32
                        sourceSize.height: 32
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }
                    Text {
                        x: 38
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 48
                        text: sourceLink.modelData.label
                        textFormat: Text.PlainText
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    Popup {
        id: contributorsPopup
        objectName: "vinylSourceAttributionContributorsPopup"
        parent: root.popupHost
        width: Math.max(0, Math.min(360, root.popupViewportWidth - 16))
        height: Math.max(0, Math.min(310, root.popupViewportHeight - 16))
        padding: 14
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 14
            color: "#171a22"
            border.width: 1
            border.color: theme.edge
        }
        onOpened: contributorsScroll.forceActiveFocus(Qt.PopupFocusReason)
        onClosed: root.restoreFocusFromPopup()
        contentItem: Column {
            spacing: 10
            Text {
                id: contributorsTitle
                objectName: "vinylSourceAttributionContributorsTitle"
                text: "ANNOTATION CONTRIBUTORS"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 11
                font.letterSpacing: 1.2
            }
            ListView {
                id: contributorsScroll
                objectName: "vinylSourceAttributionContributorsScroll"
                width: parent.width
                height: Math.max(0, parent.height - contributorsTitle.implicitHeight - 10)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                focus: true
                activeFocusOnTab: true
                Accessible.role: Accessible.Pane
                Accessible.name: "Annotation contributors"
                model: root.displayedContributors
                spacing: 8
                Keys.onPressed: event => root.handleScrollKey(contributorsScroll, event)
                delegate: Item {
                    id: contributorRow
                    required property int index
                    required property var modelData
                    objectName: "vinylSourceAttributionContributor_" + index
                    width: contributorsScroll.width
                    height: 34
                    readonly property string profileUrl: modelData.profileUrl
                    KeyboardAction {
                        id: profileAction
                        objectName: contributorRow.objectName + "Link"
                        anchors.fill: parent
                        visible: contributorRow.profileUrl.length > 0
                        accessibleName: contributorRow.modelData.name + " on Genius"
                        Accessible.role: Accessible.Link
                        ToolTip.text: accessibleName
                        ToolTip.visible: hovered
                        onTriggered: root.requestContributorProfile(contributorRow.profileUrl)
                        Rectangle {
                            anchors.fill: parent
                            radius: 7
                            color: profileAction.interactionActive ? theme.glassHi : "transparent"
                        }
                        Text {
                            objectName: contributorRow.objectName + "LinkName"
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            verticalAlignment: Text.AlignVCenter
                            text: contributorRow.modelData.name
                            textFormat: Text.PlainText
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 14
                            elide: Text.ElideRight
                            font.underline: profileAction.visible
                        }
                    }
                    Text {
                        objectName: contributorRow.objectName + "PlainName"
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: Text.AlignVCenter
                        visible: contributorRow.profileUrl.length === 0
                        text: contributorRow.modelData.name
                        textFormat: Text.PlainText
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 14
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
