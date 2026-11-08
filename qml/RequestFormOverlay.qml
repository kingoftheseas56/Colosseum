import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import "ViewportAvoidance.js" as Viewport
import "SystemFocusContainment.js" as FocusContainment

FocusScope {
    id: root
    objectName: "requestFormOverlay"

    property string kind: "bug"
    property url backdropUrl
    property var availableUniverses: []
    property var service: typeof FeedbackRequests !== "undefined" ? FeedbackRequests : null
    property var drafts: ({})
    property bool touchInput: Qt.platform.os === "android"
    property var inputMethod: Qt.inputMethod
    property var keyboardPlatform: typeof PlatformRuntime !== "undefined" ? PlatformRuntime : null
    signal closeRequested()
    signal draftChanged()

    Theme { id: theme }

    readonly property bool universe: kind === "universe"
    readonly property bool feature: kind === "feature"
    readonly property bool keyboardVisible: touchInput && !!(inputMethod && inputMethod.visible)
    readonly property rect availableRect: {
        const keyboard = keyboardVisible ? inputMethod.keyboardRectangle : Qt.rect(0, 0, 0, 0)
        const origin = mapFromItem(null, keyboard.x, keyboard.y)
        const nativeTop = keyboardVisible && keyboardPlatform ? keyboardPlatform.dockedKeyboardTop : -1
        const top = nativeTop >= 0 ? mapFromItem(null, 0, nativeTop).y : -1
        const obstruction = nativeTop >= 0
            ? {x: 0, y: top, width: width, height: Math.max(1, height - top)}
            : {x: origin.x, y: origin.y, width: keyboard.width, height: keyboard.height}
        const box = Viewport.available(width, height, SafeArea.margins, keyboardVisible, obstruction,
            keyboardVisible ? 6 : touchInput ? 12 : 28)
        return Qt.rect(box.x, box.y, box.width, box.height)
    }
    readonly property string heading: universe ? qsTr("Request a Universe")
        : feature ? qsTr("Feature Request") : qsTr("Report a Bug")
    readonly property var formFields: {
        if (universe)
            return [{key: "title", label: qsTr("Franchise name"), hint: qsTr("e.g. Godzilla"), limit: 120}]
        const fields = [
            {key: "title", label: feature ? qsTr("Feature name") : qsTr("Short summary"),
             hint: feature ? qsTr("Give your idea a short title") : qsTr("Give the issue a short title"), limit: 120},
            {key: "message", label: feature ? qsTr("What would you like to do?") : qsTr("What went wrong?"),
             hint: feature ? qsTr("Describe the feature in your own words.") : qsTr("Describe the problem in your own words."), limit: 4000, height: 108},
            {key: "email", label: qsTr("Reply email (optional)"), hint: qsTr("Only if you’d like a reply"), limit: 254}
        ]
        if (feature) {
            fields.push({key: "benefit", label: qsTr("How would this help? (optional)"),
                         hint: qsTr("Tell us when you would use it or what it would make easier."), limit: 2500, height: 76})
        } else {
            fields.push({key: "steps_to_reproduce", label: qsTr("Steps to reproduce (optional)"),
                         hint: qsTr("What did you do before the problem happened?"), limit: 2500, height: 76})
            fields.push({key: "expected_behavior", label: qsTr("What did you expect? (optional)"),
                         hint: qsTr("What should have happened instead?"), limit: 2000, height: 60})
        }
        return fields
    }

    property var values: ({})
    property var selectedArticle: null
    property bool manualName: false
    property var articles: []
    property string statusText: ""
    property bool searching: false
    property bool sending: false
    property bool sent: false
    property bool ready: false
    property Item focusedEditor: null
    property string reference: ""
    readonly property string query: (values.title || "").trim()
    readonly property var catalogueTitles: (availableUniverses || []).map(function(entry) {
        return root.catalogueKey(typeof entry === "string" ? entry : entry.title || entry.name)
    })
    readonly property var suggestions: articles.filter(function(article) {
        return !root.alreadyAvailable(article.title)
    })
    readonly property bool canSend: !!service && !sending && !sent && query.length > 0
        && (universe
            ? (selectedArticle && !alreadyAvailable(selectedArticle.title)) || (manualName && !alreadyAvailable(query))
            : (values.message || "").trim().length >= 20)

    function catalogueKey(title) {
        return String(title || "").trim().replace(/ \(franchise\)$/i, "").toLowerCase()
    }

    function alreadyAvailable(title) {
        return catalogueTitles.includes(catalogueKey(title))
    }

    function saveDraft() {
        drafts[kind] = {values: values, article: selectedArticle, manualName: manualName, reference: reference}
        draftChanged()
    }

    function restoreDraft() {
        const draft = drafts[kind]
        values = draft ? draft.values : ({})
        selectedArticle = draft ? draft.article : null
        manualName = draft ? draft.manualName : false
        reference = draft ? draft.reference || "" : ""
        articles = []
        sent = false
        sending = false
        statusText = ""
        scheduleSearch()
        Qt.callLater(root.focusInitialControl)
    }

    function focusInitialControl() {
        const first = fields.itemAt(0)
        const activeItem = root.Window.window ? root.Window.window.activeFocusItem : null
        if (activeItem !== root && FocusContainment.isWithin(activeItem, root))
            return
        if (!touchInput && first && first.editor)
            first.editor.forceActiveFocus()
        else
            closeButton.forceActiveFocus()
    }

    function scheduleSearch() {
        wikiSearch.stop()
        searching = universe && !!service && query.length >= 2
        if (searching)
            wikiSearch.start()
    }

    function updateValue(key, text) {
        if (text === (values[key] || ""))
            return
        values = Object.assign({}, values, {[key]: text})
        reference = ""
        statusText = ""
        if (universe && key === "title") {
            selectedArticle = null
            manualName = false
            articles = []
            scheduleSearch()
        }
        saveDraft()
    }

    function focusEditor(editor, viewport) {
        if (editor.activeFocus)
            focusedEditor = viewport || editor
        else if (focusedEditor === (viewport || editor))
            focusedEditor = null
        Qt.callLater(root.revealEditor)
    }

    function revealEditor() {
        if (!focusedEditor || form.height <= 0)
            return
        const top = focusedEditor.mapToItem(form.contentItem, 0, 0).y
        const bottom = top + focusedEditor.height
        let offset = form.contentY
        if (keyboardVisible)
            offset = bottom - form.height
        else if (top < offset)
            offset = top
        else if (bottom > offset + form.height)
            offset = bottom - form.height
        form.contentY = Math.max(0, Math.min(offset, form.contentHeight - form.height))
    }

    function send() {
        if (!canSend)
            return
        const email = (values.email || "").trim()
        if (email && !/^[^@\s]+@[^@\s]+\.[^@\s]+$/.test(email)) {
            statusText = qsTr("Enter a valid reply email or leave it blank.")
            return
        }
        const payload = universe
            ? {title: selectedArticle ? selectedArticle.title : query, message: query}
            : Object.assign({}, values, {title: query, message: values.message.trim(), email: email})
        if (universe && selectedArticle)
            payload.wikipedia_page_id = selectedArticle.pageid
        if (!reference && typeof service.newReference === "function") {
            reference = service.newReference()
            saveDraft()
        }
        if (reference)
            payload.reference = reference
        statusText = ""
        sending = true
        service.submit(kind, payload)
    }

    onKindChanged: if (ready) restoreDraft()
    onAvailableRectChanged: Qt.callLater(root.revealEditor)
    Keys.onTabPressed: function(event) {
        event.accepted = FocusContainment.move(root.Window.window, root, true)
    }
    Keys.onBacktabPressed: function(event) {
        event.accepted = FocusContainment.move(root.Window.window, root, false)
    }
    Component.onCompleted: {
        if (touchInput && service && typeof service.setInlineKeyboard === "function")
            service.setInlineKeyboard(true)
        ready = true
        restoreDraft()
    }
    Component.onDestruction: {
        if (touchInput && service && typeof service.setInlineKeyboard === "function")
            service.setInlineKeyboard(false)
    }

    Timer {
        interval: 100
        repeat: true
        triggeredOnStart: true
        running: root.keyboardVisible && !!root.keyboardPlatform
        onTriggered: root.keyboardPlatform.sampleDockedKeyboard()
    }

    Timer {
        id: wikiSearch
        interval: 300
        onTriggered: if (root.service) root.service.searchUniverses(root.query)
    }

    Connections {
        target: root.service
        function onUniverseResults(query, rows) {
            if (!root.universe || query !== root.query)
                return
            root.searching = false
            root.articles = rows || []
        }
        function onSubmissionFinished(ok, message) {
            if (!root.sending)
                return
            root.sending = false
            root.sent = ok
            root.statusText = message || (ok ? qsTr("Thank you. Your request was sent.") : qsTr("Could not send your request."))
            if (ok) {
                delete root.drafts[root.kind]
                root.draftChanged()
                closeButton.forceActiveFocus()
                if (root.keyboardVisible)
                    root.inputMethod.hide()
            }
        }
    }

    Image {
        anchors.fill: parent
        source: root.backdropUrl
        sourceSize: Qt.size(width, height)
        visible: status === Image.Ready
        layer.enabled: visible
        layer.effect: MultiEffect { blurEnabled: true; blurMax: 9; blur: 1; autoPaddingEnabled: false }
    }
    Rectangle { anchors.fill: parent; color: "#700a101a" }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: function(wheel) { wheel.accepted = true }
    }

    Rectangle {
        id: panel
        objectName: "requestFormPanel"
        x: root.availableRect.x + (root.availableRect.width - width) / 2
        y: root.availableRect.y + (root.availableRect.height - height) / 2
        width: Math.min(root.availableRect.width, root.keyboardVisible ? 560 : 880)
        height: root.keyboardVisible ? root.availableRect.height : Math.min(root.availableRect.height, 720)
        gradient: Gradient {
            GradientStop { position: 0; color: "#f5303337" }
            GradientStop { position: 1; color: "#f5181c20" }
        }
        border.color: "#30ffffff"
        radius: root.keyboardVisible ? 12 : 20

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: root.keyboardVisible ? 4 : 22
            anchors.leftMargin: root.keyboardVisible ? 10 : root.width < 660 ? 20 : 36
            anchors.rightMargin: anchors.leftMargin
            spacing: root.keyboardVisible ? 2 : 10

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: root.keyboardVisible ? 26 : 72
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: root.keyboardVisible ? 0 : 8
                    Label {
                        visible: !root.keyboardVisible
                        text: "COLOSSEUM"
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 12
                        font.letterSpacing: 2.4
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.heading
                        elide: Text.ElideRight
                        color: theme.ink
                        font.family: root.keyboardVisible ? theme.ui : theme.display
                        font.pixelSize: root.keyboardVisible ? 15 : 36
                    }
                }
                Button {
                    id: closeButton
                    objectName: "requestFormClose"
                    Layout.alignment: root.keyboardVisible ? Qt.AlignVCenter : Qt.AlignTop
                    Layout.preferredHeight: root.keyboardVisible ? 26 : 40
                    Layout.preferredWidth: root.keyboardVisible ? implicitWidth : 40
                    text: root.keyboardVisible ? qsTr("Close") : "×"
                    Accessible.name: qsTr("Close")
                    ToolTip.visible: closeButton.hovered && !root.keyboardVisible
                    ToolTip.delay: 350
                    ToolTip.text: qsTr("Close")
                    enabled: !root.sending
                    onClicked: root.closeRequested()
                    background: Rectangle {
                        radius: height / 2
                        color: closeButton.down || closeButton.hovered ? "#22ffffff" : "#08ffffff"
                        border.color: "#30ffffff"
                    }
                    contentItem: Label {
                        text: closeButton.text
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: root.keyboardVisible ? 13 : 24
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: !root.keyboardVisible && !root.sent
                wrapMode: Text.WordWrap
                color: "#bdc8da"
                font.family: theme.ui
                text: root.universe ? qsTr("Search Wikipedia for the whole franchise you’d like to see in Universes.")
                    : root.feature ? qsTr("Tell us what you’d like to see in Colosseum.")
                                   : qsTr("Tell us what happened so we can look into it.")
            }

            Flickable {
                id: form
                objectName: "requestFormScroll"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                visible: !root.sent
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: width
                contentHeight: body.implicitHeight
                onHeightChanged: Qt.callLater(root.revealEditor)
                onContentHeightChanged: Qt.callLater(root.revealEditor)
                ScrollBar.vertical: ScrollBar { }

                ColumnLayout {
                    id: body
                    width: form.width - 8
                    spacing: root.touchInput ? 8 : 18

                    Repeater {
                        id: fields
                        model: root.formFields
                        ColumnLayout {
                            id: field
                            required property var modelData
                            readonly property var spec: modelData
                            readonly property Item editor: editorLoader.status === Loader.Ready ? editorLoader.item.editor : null
                            Layout.fillWidth: true
                            spacing: 4

                            Label {
                                Layout.fillWidth: true
                                text: field.spec.label
                                color: "#e4ecf8"
                                font.family: theme.ui
                                wrapMode: Text.WordWrap
                            }
                            Loader {
                                id: editorLoader
                                Layout.fillWidth: true
                                Layout.preferredHeight: status === Loader.Ready ? item.implicitHeight : 0
                                sourceComponent: field.spec.height ? multiline : singleLine
                            }

                            Component {
                                id: singleLine
                                TextField {
                                    id: line
                                    readonly property Item editor: line
                                    objectName: "requestField_" + field.spec.key
                                    implicitHeight: root.keyboardVisible ? Math.max(0, Math.min(34, form.height))
                                        : root.touchInput ? 38 : 50
                                    topPadding: 3
                                    bottomPadding: 3
                                    leftPadding: 10
                                    rightPadding: 10
                                    text: root.values[field.spec.key] || ""
                                    placeholderText: field.spec.hint
                                    placeholderTextColor: "#a1aec3"
                                    Accessible.name: field.spec.label
                                    maximumLength: field.spec.limit
                                    enabled: !root.sending
                                    color: "#f5f7fc"
                                    Binding on font.family { when: !root.touchInput; value: theme.ui }
                                    Binding on font.pixelSize { when: !root.touchInput; value: 17 }
                                    inputMethodHints: field.spec.key === "email" ? Qt.ImhEmailCharactersOnly : Qt.ImhNone
                                    background: FieldBackground { focused: line.activeFocus }
                                    onTextEdited: root.updateValue(field.spec.key, text)
                                    onActiveFocusChanged: root.focusEditor(line)
                                }
                            }

                            Component {
                                id: multiline
                                Flickable {
                                    id: editorViewport
                                    objectName: "requestViewport_" + field.spec.key
                                    readonly property Item editor: area
                                    implicitHeight: root.keyboardVisible
                                        ? Math.max(0, Math.min(form.height, Math.max(34, area.contentHeight + area.topPadding + area.bottomPadding)))
                                        : field.spec.height
                                    clip: true
                                    boundsBehavior: Flickable.StopAtBounds
                                    TextArea.flickable: TextArea {
                                        id: area
                                        objectName: "requestField_" + field.spec.key
                                        topPadding: 6
                                        bottomPadding: 6
                                        leftPadding: 10
                                        rightPadding: 10
                                        text: root.values[field.spec.key] || ""
                                        placeholderText: field.spec.hint
                                        placeholderTextColor: "#a1aec3"
                                        Accessible.name: field.spec.label
                                        wrapMode: TextEdit.Wrap
                                        enabled: !root.sending
                                        color: "#f5f7fc"
                                        Binding on font.family { when: !root.touchInput; value: theme.ui }
                                        Binding on font.pixelSize { when: !root.touchInput; value: 14 }
                                        background: FieldBackground { focused: area.activeFocus }
                                        onTextChanged: {
                                            if (activeFocus && length > field.spec.limit)
                                                remove(field.spec.limit, length)
                                            if (activeFocus)
                                                root.updateValue(field.spec.key, text)
                                        }
                                        onActiveFocusChanged: root.focusEditor(area, editorViewport)
                                    }
                                }
                            }
                        }
                    }

                    Label {
                        visible: root.universe
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#bdc8da"
                        text: root.searching ? qsTr("Searching Wikipedia…")
                            : root.suggestions.length ? qsTr("Choose the page for the whole franchise")
                            : root.alreadyAvailable(root.query) ? qsTr("This franchise is already in Universes.")
                            : root.query.length < 2 ? qsTr("Enter a franchise name to find matching pages.")
                                                   : qsTr("No matching pages for a new Universe.")
                    }
                    Repeater {
                        model: root.universe ? root.suggestions : []
                        Button {
                            id: suggestion
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.title
                            checkable: true
                            checked: !!root.selectedArticle && root.selectedArticle.pageid === modelData.pageid
                            onClicked: {
                                root.selectedArticle = modelData
                                root.manualName = false
                                root.reference = ""
                                root.saveDraft()
                            }
                            contentItem: ColumnLayout {
                                Label {
                                    Layout.fillWidth: true
                                    text: suggestion.text
                                    textFormat: Text.PlainText
                                    wrapMode: Text.WordWrap
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: suggestion.modelData.extract || ""
                                    textFormat: Text.PlainText
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                    opacity: 0.75
                                }
                            }
                        }
                    }
                    CheckBox {
                        visible: root.universe && root.query.length > 0 && !root.alreadyAvailable(root.query)
                        Layout.fillWidth: true
                        text: qsTr("Use the franchise name I typed")
                        checked: root.manualName
                        onToggled: {
                            root.manualName = checked
                            root.selectedArticle = null
                            root.reference = ""
                            root.saveDraft()
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: !root.keyboardVisible
                        text: root.universe
                            ? qsTr("Your franchise request, app version, and platform are sent privately to the Colosseum team.")
                            : qsTr("Your text, app version, and platform are sent privately to the Colosseum team. If you include an email, we can reply there.")
                        font.family: theme.ui
                        font.pixelSize: 10
                        color: "#a6aab2"
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.fillHeight: root.sent
                visible: root.statusText.length > 0 && !root.keyboardVisible
                color: root.sent ? "#b3eacb" : "#ffc0b3"
                text: root.statusText
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                verticalAlignment: Text.AlignVCenter
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: root.keyboardVisible ? 28 : root.touchInput ? 36 : 44
                visible: !root.sent
                Label {
                    Layout.fillWidth: true
                    visible: root.keyboardVisible
                    color: "#ffc0b3"
                    text: root.statusText
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    visible: !root.keyboardVisible
                    text: root.touchInput ? "" : qsTr("Draft saved on this device.")
                    color: "#aeb2bc"
                    font.family: theme.ui
                    font.pixelSize: 11
                }
                FormButton {
                    objectName: "requestFormCancel"
                    visible: !root.touchInput
                    Layout.preferredHeight: 44
                    text: qsTr("Cancel")
                    enabled: !root.sending
                    onClicked: root.closeRequested()
                }
                FormButton {
                    objectName: "requestFormSend"
                    Layout.preferredHeight: root.keyboardVisible ? 28 : root.touchInput ? 36 : 44
                    Layout.minimumWidth: 90
                    primary: true
                    text: root.sending ? qsTr("Sending…") : root.touchInput || root.universe ? qsTr("Send")
                        : root.feature ? qsTr("Submit request") : qsTr("Submit report")
                    enabled: root.canSend
                    onClicked: root.send()
                }
            }
            Label {
                Layout.fillWidth: true
                visible: !root.touchInput && !root.sent
                text: qsTr("You can send feedback without an account.")
                color: "#9da3ad"
                font.family: theme.ui
                font.pixelSize: 10
            }
        }
    }

    component FieldBackground: Rectangle {
        property bool focused: false
        radius: 8
        color: "#5e0b0f12"
        border.color: focused ? "#f0c44a" : "#45ffffff"
    }

    component FormButton: Button {
        id: button
        property bool primary: false
        leftPadding: 14
        rightPadding: 14
        topPadding: 2
        bottomPadding: 2
        contentItem: Label {
            text: button.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: theme.ui
            font.pixelSize: 13
            font.bold: button.primary
            color: !button.enabled ? "#858a93" : button.primary ? "#201a0a" : "#d1d0d5"
        }
        background: Rectangle {
            radius: 10
            color: !button.enabled ? "#0bffffff"
                : button.primary ? button.hovered ? "#f7d268" : "#f0c44a" : "#0affffff"
            border.width: button.activeFocus ? 2 : 1
            border.color: button.activeFocus || button.enabled && button.primary ? "#f0c44a" : "#30ffffff"
        }
    }
}
