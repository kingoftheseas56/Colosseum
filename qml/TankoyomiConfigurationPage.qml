// The in-app Tankoyomi configuration surface. Inventory and durable policy come from the
// native Manga bridge; this component only presents that state and emits user intent through
// the bridge methods. No provider names or quality claims are copied from the visual oracle.
pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root
    objectName: "tankoyomiConfigurationPage"

    property Item backdrop: null
    property var mangaRef: (typeof Manga !== "undefined") ? Manga : null
    property var extensionsRef: (typeof Extensions !== "undefined") ? Extensions : null
    property string activeTab: "configuration"
    property string selectedLanguage: ""
    property var languageRows: []
    property var providerRows: []
    property bool languageMenuOpen: false
    property int extensionRevision: root.extensionsRef ? root.extensionsRef.revision : 0
    property Item focusReturnItem: null

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal searchClicked()

    readonly property bool tankoyomiEnabled: {
        var ignoredRevision = root.extensionRevision
        var source = root.extensionsRef
        if (!source || !source.installed) return false
        var rows = source.installed() || []
        for (var i = 0; i < rows.length; ++i) {
            if (rows[i] && String(rows[i].id || "") === "colosseum.well.tankoyomi")
                return rows[i].enabled === true
        }
        return false
    }

    function takeKeyboardFocus() {
        root.focusReturnItem = root.Window.window ? root.Window.window.activeFocusItem : null
        page.forceActiveFocus(Qt.TabFocusReason)
    }

    function _languageCode(row) {
        return String(row && row.code || "").trim().toLowerCase()
    }

    function _defaultLanguage() {
        var source = root.mangaRef
        if (!source) return ""
        var value = typeof source.chapterDefaultLanguage === "function"
            ? source.chapterDefaultLanguage() : source.chapterDefaultLanguage
        return String(value || "").trim().toLowerCase()
    }

    function refresh() {
        var source = root.mangaRef
        var rows = []
        if (source && source.chapterLanguages) {
            rows = typeof source.chapterLanguages === "function"
                ? (source.chapterLanguages() || []) : (source.chapterLanguages || [])
        }
        root.languageRows = rows

        var defaultCode = root._defaultLanguage()
        var chosen = root.selectedLanguage
        var found = false
        for (var i = 0; i < rows.length; ++i) {
            var code = root._languageCode(rows[i])
            if (code === chosen) found = true
        }
        if (!found) {
            chosen = defaultCode
            for (var j = 0; j < rows.length; ++j) {
                if (root._languageCode(rows[j]) === chosen) {
                    found = true
                    break
                }
            }
        }
        if (!found && rows.length) chosen = root._languageCode(rows[0])
        root.selectedLanguage = chosen
        root._refreshProviders()
    }

    function _refreshProviders() {
        var source = root.mangaRef
        if (!source || !root.selectedLanguage.length || !source.chapterProviders) {
            root.providerRows = []
            return
        }
        root.providerRows = typeof source.chapterProviders === "function"
            ? (source.chapterProviders(root.selectedLanguage) || []) : []
    }

    function selectLanguage(code) {
        var next = String(code || "").trim().toLowerCase()
        if (!next || next === root.selectedLanguage) {
            root.languageMenuOpen = false
            return
        }
        root.selectedLanguage = next
        root.languageMenuOpen = false
        root._refreshProviders()
    }

    function setDefaultLanguage(code) {
        var source = root.mangaRef
        if (!source || !source.setChapterDefaultLanguage) return false
        var next = String(code || "").trim().toLowerCase()
        if (!next.length || !source.setChapterDefaultLanguage(next)) return false
        root.selectLanguage(next)
        root.refresh()
        return true
    }

    function toggleProvider(language, providerId, enabled) {
        var source = root.mangaRef
        if (!source || !source.setChapterProviderEnabled) return false
        var ok = source.setChapterProviderEnabled(language, providerId, enabled)
        if (ok) root.refresh()
        return ok
    }

    function moveProvider(language, providerId, direction) {
        var source = root.mangaRef
        if (!source) return false
        var method = direction < 0 ? source.moveChapterProviderUp : source.moveChapterProviderDown
        if (!method) return false
        var ok = method(language, providerId)
        if (ok) root._refreshProviders()
        return ok
    }

    function moveProviderUp(language, providerId) {
        return root.moveProvider(language, providerId, -1)
    }

    function moveProviderDown(language, providerId) {
        return root.moveProvider(language, providerId, 1)
    }

    function resetProviderOrder(language) {
        var source = root.mangaRef
        if (!source || !source.resetChapterProviderOrder) return false
        var ok = source.resetChapterProviderOrder(language)
        if (ok) root._refreshProviders()
        return ok
    }

    function setMasterEnabled(enabled) {
        var source = root.extensionsRef
        if (!source || !source.setEnabled) return false
        source.setEnabled("colosseum.well.tankoyomi", enabled)
        root.extensionRevision = source.revision
        return true
    }

    function closeFromPage() {
        var target = root.focusReturnItem
        root.focusReturnItem = null
        root.backRequested()
        if (target && target.visible && target.enabled)
            Qt.callLater(function() { target.forceActiveFocus(Qt.BacktabFocusReason) })
    }

    Connections {
        target: root.mangaRef
        function onChapterConfigurationChanged() { root.refresh() }
    }
    Connections {
        target: root.extensionsRef
        function onChanged() {
            root.extensionRevision = root.extensionsRef ? root.extensionsRef.revision : 0
        }
    }
    Component.onCompleted: root.refresh()

    Theme { id: theme }

    Rectangle { anchors.fill: parent; color: "#000000" }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: root.backdrop
        live: true
        hideSource: false
        visible: root.backdrop !== null
    }
    Image {
        anchors.fill: parent
        source: "../assets/wallpaper/captured-motion.jpg"
        fillMode: Image.PreserveAspectCrop
        visible: root.backdrop === null
        opacity: 0.78
    }
    Rectangle { anchors.fill: parent; color: Qt.rgba(0.03, 0.04, 0.07, 0.86) }

    Flickable {
        id: page
        objectName: "tankoyomiConfigurationScroll"
        anchors.fill: parent
        anchors.leftMargin: theme.margin
        anchors.rightMargin: theme.margin
        anchors.topMargin: 60
        anchors.bottomMargin: 20
        contentWidth: width
        contentHeight: contentColumn.implicitHeight + 64
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true

        Column {
            id: contentColumn
            width: page.width
            spacing: 0

            Row {
                width: parent.width
                spacing: 18
                Item {
                    width: parent.width - masterColumn.width - 18
                    implicitHeight: headerColumn.implicitHeight
                    Column {
                        id: headerColumn
                        width: parent.width
                        spacing: 0
                        Text { text: "COLOSSEUM · STORE · CONFIGURATION"; color: theme.inkDimmer
                               font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.6; font.weight: Font.DemiBold }
                        Row {
                            spacing: 16
                            topPadding: 8
                            Text { text: "T"; width: 58; height: 58; horizontalAlignment: Text.AlignHCenter
                                   verticalAlignment: Text.AlignVCenter; color: theme.gold
                                   font.family: theme.display; font.pixelSize: 34
                                   font.weight: Font.DemiBold
                                   Rectangle { anchors.fill: parent; radius: 14; color: Qt.rgba(1,1,1,0.06)
                                              border.width: 1; border.color: theme.edge; z: -1 } }
                            Text { text: "Tankoyomi"; color: theme.ink; anchors.verticalCenter: parent.verticalCenter
                                   font.family: theme.display; font.pixelSize: 48; font.letterSpacing: -1 }
                        }
                        Item { width: 1; height: 18 }
                        Rectangle { width: 34; height: 3; radius: 2; color: theme.gold }
                        Text { width: parent.width; topPadding: 15; wrapMode: Text.WordWrap
                               text: "Configure chapter languages and the source order Tankoyomi uses inside Chapter Mode. Auto-pick follows the provider ladder from top to bottom and never crosses into another language."
                               color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13 }
                    }
                }
                Column {
                    id: masterColumn
                    width: Math.min(260, parent.width * 0.27)
                    spacing: 10
                    anchors.verticalCenter: parent.verticalCenter
                    Row {
                        spacing: 12
                        anchors.right: parent.right
                        Text { text: "Tankoyomi"; color: theme.ink; font.family: theme.ui; font.pixelSize: 15
                               font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                        Item {
                            id: masterSwitch
                            objectName: "tankoyomiMasterSwitch"
                            width: 44; height: 24
                            Accessible.role: Accessible.CheckBox
                            Accessible.name: "Enable Tankoyomi"
                            Rectangle { anchors.fill: parent; radius: 12
                                color: root.tankoyomiEnabled ? theme.gold : Qt.rgba(1,1,1,0.14)
                                border.width: 1; border.color: root.tankoyomiEnabled ? theme.gold : theme.edge }
                            Rectangle { width: 18; height: 18; radius: 9; y: 3
                                x: root.tankoyomiEnabled ? 23 : 3
                                color: root.tankoyomiEnabled ? "#1a1306" : theme.inkDim
                                Behavior on x { NumberAnimation { duration: 140 } } }
                            MouseArea { anchors.fill: parent; onClicked: root.setMasterEnabled(!root.tankoyomiEnabled) }
                            KeyboardAction { anchors.fill: parent; pointerEnabled: false; focusEnabled: true
                                focusRadius: 12; accessibleName: root.tankoyomiEnabled ? "Disable Tankoyomi" : "Enable Tankoyomi"
                                onTriggered: root.setMasterEnabled(!root.tankoyomiEnabled) }
                        }
                    }
                    Text { text: root.tankoyomiEnabled ? "Language-aware manga chapter router" : "Tankoyomi is off"
                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                           horizontalAlignment: Text.AlignRight; width: parent.width }
                }
            }

            Row {
                id: tabs
                objectName: "tankoyomiConfigurationTabs"
                width: parent.width
                topPadding: 32
                height: 78
                spacing: 28
                activeFocusOnTab: true
                property int currentIndex: root.activeTab === "about" ? 1 : 0
                function selectIndex(i) {
                    currentIndex = Math.max(0, Math.min(1, i))
                    root.activeTab = currentIndex === 1 ? "about" : "configuration"
                }
                Keys.onLeftPressed: selectIndex(currentIndex - 1)
                Keys.onRightPressed: selectIndex(currentIndex + 1)
                Repeater {
                    model: [ { key: "configuration", label: "Configuration" }, { key: "about", label: "About" } ]
                    delegate: Item {
                        id: tab
                        required property var modelData
                        required property int index
                        implicitWidth: tabText.implicitWidth
                        implicitHeight: 42
                        Text { id: tabText; text: tab.modelData.label; color: root.activeTab === tab.modelData.key ? theme.ink : theme.inkDimmer
                               font.family: theme.ui; font.pixelSize: 14; font.weight: root.activeTab === tab.modelData.key ? Font.DemiBold : Font.Normal }
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                    height: 2; radius: 1; color: theme.gold; visible: root.activeTab === tab.modelData.key }
                        MouseArea { anchors.fill: parent; onClicked: tabs.selectIndex(tab.index) }
                        KeyboardAction { anchors.fill: parent; pointerEnabled: false; focusEnabled: true
                            accessibleName: tab.modelData.label; onTriggered: tabs.selectIndex(tab.index) }
                    }
                }
            }

            Item {
                width: parent.width
                visible: root.activeTab === "about"
                height: visible ? aboutColumn.implicitHeight + 36 : 0
                Column {
                    id: aboutColumn
                    width: parent.width
                    topPadding: 28
                    spacing: 14
                    Text { text: "About Tankoyomi"; color: theme.ink; font.family: theme.display; font.pixelSize: 28 }
                    Text { width: parent.width; wrapMode: Text.WordWrap
                           text: "Tankoyomi is the language-aware chapter router used by Colosseum Chapter Mode. Its provider inventory comes from the bundled manifest, while your language, enablement, and priority choices are saved locally."
                           color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14 }
                    Text { width: parent.width; wrapMode: Text.WordWrap
                           text: "Auto-pick tries the highest enabled provider first, then continues down the selected language’s ladder. Unsupported languages never fall through to another language."
                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13 }
                }
            }

            Row {
                id: configurationBody
                visible: root.activeTab === "configuration"
                width: parent.width
                topPadding: 26
                spacing: 18
                Item {
                    id: languagePanel
                    objectName: "tankoyomiLanguagePanel"
                    width: Math.max(310, (parent.width - 18) * 0.34)
                    implicitHeight: languageColumn.implicitHeight
                    Rectangle { anchors.fill: parent; radius: 18; color: Qt.rgba(0.04,0.05,0.08,0.78)
                                border.width: 1; border.color: theme.edge }
                    Column {
                        id: languageColumn
                        width: parent.width
                        padding: 20
                        spacing: 0
                        Text { text: "Languages"; color: theme.ink; font.family: theme.display; font.pixelSize: 24 }
                        Text { text: "Choose which chapter languages Tankoyomi can use."; topPadding: 6
                               color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13 }
                        Item { width: 1; height: 12 }
                        Column {
                            id: languageList
                            objectName: "tankoyomiLanguageList"
                            width: parent.width
                            spacing: 5
                            Repeater {
                                model: root.languageRows
                                delegate: Item {
                                    id: langRow
                                    required property var modelData
                                    required property int index
                                    objectName: "tankoyomiLanguage_" + root._languageCode(modelData)
                                    width: languageList.width
                                    height: 58
                                    property string code: root._languageCode(modelData)
                                    Rectangle { anchors.fill: parent; radius: 12
                                                color: root.selectedLanguage === langRow.code ? Qt.rgba(1,1,1,0.07) : "transparent"
                                                border.width: 1; border.color: root.selectedLanguage === langRow.code ? Qt.rgba(0.94,0.77,0.29,0.28) : "transparent" }
                                    Row {
                                        anchors.fill: parent; anchors.margins: 10; spacing: 12
                                        Text { width: 38; height: 38; text: String(langRow.code || "?").toUpperCase().slice(0,2)
                                               color: theme.gold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                               font.family: theme.ui; font.pixelSize: 12; font.weight: Font.Bold
                                               Rectangle { anchors.fill: parent; radius: 9; color: Qt.rgba(1,1,1,0.06); border.width: 1; border.color: theme.edge; z: -1 } }
                                        Column { width: parent.width - 38 - 16 - 70; anchors.verticalCenter: parent.verticalCenter; spacing: 3
                                            Text { text: langRow.modelData.label || langRow.code; color: theme.ink; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold }
                                            Text { text: langRow.code + " · " + (langRow.modelData.providerCount || 0) + " source" + ((langRow.modelData.providerCount || 0) === 1 ? "" : "s")
                                                   color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11 } }
                                        Text { width: 58; text: langRow.modelData.default ? "DEFAULT" : ((langRow.modelData.enabledProviderCount || 0) + " active")
                                               color: langRow.modelData.default ? theme.gold : theme.inkDimmer; font.family: theme.ui; font.pixelSize: 10
                                               horizontalAlignment: Text.AlignRight; anchors.verticalCenter: parent.verticalCenter }
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: root.selectLanguage(langRow.code) }
                                    KeyboardAction { anchors.fill: parent; pointerEnabled: false; focusEnabled: true
                                        accessibleName: "Select " + (langRow.modelData.label || langRow.code); onTriggered: root.selectLanguage(langRow.code) }
                                }
                            }
                        }
                        Item { width: 1; height: 14 }
                        Rectangle { width: parent.width; height: 1; color: Qt.rgba(1,1,1,0.08) }
                        Text { text: "Default chapter language"; topPadding: 16; bottomPadding: 8
                               color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                        Item {
                            id: defaultSelector
                            objectName: "tankoyomiDefaultLanguageSelector"
                            width: parent.width; height: 42
                            Rectangle { anchors.fill: parent; radius: 11; color: Qt.rgba(1,1,1,0.055); border.width: 1
                                        border.color: defaultSelector.activeFocus ? theme.gold : theme.edge }
                            Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                                   text: { for (var i=0;i<root.languageRows.length;i++) if (root._languageCode(root.languageRows[i]) === root._defaultLanguage()) return root.languageRows[i].label; return root._defaultLanguage() }
                                   color: theme.ink; font.family: theme.ui; font.pixelSize: 13 }
                            Text { anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; text: root.languageMenuOpen ? "▲" : "▼"; color: theme.inkDimmer; font.pixelSize: 11 }
                            MouseArea { anchors.fill: parent; onClicked: root.languageMenuOpen = !root.languageMenuOpen }
                            KeyboardAction { anchors.fill: parent; pointerEnabled: false; focusEnabled: true; accessibleName: "Default chapter language"
                                onTriggered: root.languageMenuOpen = !root.languageMenuOpen }
                        }
                        Column {
                            id: defaultMenu
                            objectName: "tankoyomiDefaultLanguageMenu"
                            visible: root.languageMenuOpen
                            width: parent.width
                            spacing: 2
                            topPadding: 4
                            Repeater {
                                model: root.languageRows
                                delegate: Item {
                                    id: defaultRow
                                    required property var modelData
                                    width: defaultMenu.width; height: 34
                                    property string code: root._languageCode(modelData)
                                    Text { anchors.fill: parent; anchors.leftMargin: 10; verticalAlignment: Text.AlignVCenter
                                           text: defaultRow.modelData.label || defaultRow.code; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12 }
                                    MouseArea { anchors.fill: parent; onClicked: root.setDefaultLanguage(defaultRow.code) }
                                    KeyboardAction { anchors.fill: parent; pointerEnabled: false; focusEnabled: true
                                        accessibleName: "Set default language to " + (defaultRow.modelData.label || defaultRow.code)
                                        onTriggered: root.setDefaultLanguage(defaultRow.code) }
                                }
                            }
                        }
                    }
                }

                Item {
                    id: detailPanel
                    objectName: "tankoyomiProviderPanel"
                    width: parent.width - languagePanel.width - 18
                    implicitHeight: detailColumn.implicitHeight
                    Rectangle { anchors.fill: parent; radius: 18; color: Qt.rgba(0.04,0.05,0.08,0.78)
                                border.width: 1; border.color: theme.edge }
                    Column {
                        id: detailColumn
                        width: parent.width
                        padding: 20
                        spacing: 0
                        Text { text: "Auto-pick route"; color: theme.gold; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 1.5; font.weight: Font.DemiBold }
                        Text { text: { for (var i=0;i<root.languageRows.length;i++) if (root._languageCode(root.languageRows[i]) === root.selectedLanguage) return root.languageRows[i].label; return root.selectedLanguage }
                               color: theme.ink; font.family: theme.display; font.pixelSize: 25; topPadding: 5 }
                        Text { text: root.providerRows.length + " configured source" + (root.providerRows.length === 1 ? "" : "s") + " · same-language fallback only"
                               color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13; topPadding: 5 }
                        Row {
                            width: parent.width
                            topPadding: 18; bottomPadding: 12
                            spacing: 10
                            Text { text: "Highest active provider is tried first. Reorder the ladder to change priority."; width: parent.width - resetButton.width - 10
                                   color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12; wrapMode: Text.WordWrap }
                            Rectangle {
                                id: resetButton
                                objectName: "tankoyomiResetOrder"
                                width: 112; height: 34; radius: 10; color: Qt.rgba(1,1,1,0.05); border.width: 1; border.color: theme.edge
                                Text { anchors.centerIn: parent; text: "Reset order"; color: resetInput.interactionActive ? theme.ink : theme.inkDim; font.family: theme.ui; font.pixelSize: 12 }
                                MouseArea { anchors.fill: parent; onClicked: root.resetProviderOrder(root.selectedLanguage) }
                                KeyboardAction { id: resetInput; anchors.fill: parent; pointerEnabled: false; focusEnabled: true; accessibleName: "Reset provider order"
                                    onTriggered: root.resetProviderOrder(root.selectedLanguage) }
                            }
                        }
                        Column {
                            id: providerLadder
                            objectName: "tankoyomiProviderLadder"
                            width: parent.width
                            spacing: 9
                            Repeater {
                                model: root.providerRows
                                delegate: Item {
                                    id: providerRow
                                    required property var modelData
                                    required property int index
                                    property string providerId: String(modelData.id || "")
                                    property bool providerEnabled: modelData.enabled === true
                                    property bool canMoveUp: index > 0
                                    property bool canMoveDown: index < root.providerRows.length - 1
                                    objectName: "tankoyomiProvider_" + providerId
                                    width: providerLadder.width
                                    height: 86
                                    opacity: providerEnabled ? 1 : 0.48
                                    Rectangle { anchors.fill: parent; radius: 14; color: Qt.rgba(1,1,1,0.035); border.width: 1; border.color: Qt.rgba(1,1,1,0.09) }
                                    Row {
                                        anchors.fill: parent; anchors.margins: 12; spacing: 12
                                         Item { width: 38; height: 62; anchors.verticalCenter: parent.verticalCenter
                                             Text { anchors.centerIn: parent; text: providerRow.index + 1; color: providerRow.index === 0 ? "#191407" : theme.gold; font.family: theme.ui; font.pixelSize: 15; font.weight: Font.Bold
                                                    Rectangle { anchors.fill: parent; anchors.margins: 3; radius: 18; color: providerRow.index === 0 ? theme.gold : "#151821"; border.width: 1; border.color: Qt.rgba(0.94,0.77,0.29,0.65); z: -1 } } }
                                         Text { width: 42; height: 42; anchors.verticalCenter: parent.verticalCenter; text: String(providerRow.modelData.name || providerRow.providerId).slice(0,1).toUpperCase()
                                               color: theme.gold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.family: theme.display; font.pixelSize: 24
                                               Rectangle { anchors.fill: parent; radius: 11; color: Qt.rgba(1,1,1,0.055); border.width: 1; border.color: theme.edge; z: -1 } }
                                        Column { width: parent.width - 38 - 42 - 120 - 36; anchors.verticalCenter: parent.verticalCenter; spacing: 5
                                            Row { spacing: 8
                                                Text { text: providerRow.modelData.name || providerRow.providerId; color: theme.ink; font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold }
                                                Text { text: providerRow.providerEnabled ? "ACTIVE" : "DISABLED"; color: providerRow.providerEnabled ? "#72d487" : theme.inkDimmer; font.family: theme.ui; font.pixelSize: 10; font.weight: Font.DemiBold } }
                                             Text { text: "Rank " + (providerRow.index + 1) + " · " + providerRow.providerId; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11 }
                                        }
                                        Row { width: 120; anchors.verticalCenter: parent.verticalCenter; spacing: 5
                                             Item { id: providerToggle; objectName: "tankoyomiProviderToggle_" + providerRow.providerId; width: 40; height: 22
                                                 Rectangle { anchors.fill: parent; radius: 11; color: providerRow.providerEnabled ? theme.gold : Qt.rgba(1,1,1,0.14); border.width: 1; border.color: providerRow.providerEnabled ? theme.gold : theme.edge }
                                                 Rectangle { width: 16; height: 16; radius: 8; y: 2.5; x: providerRow.providerEnabled ? 21 : 2.5; color: providerRow.providerEnabled ? "#1a1306" : theme.inkDim }
                                                 MouseArea { anchors.fill: parent; onClicked: root.toggleProvider(root.selectedLanguage, providerRow.providerId, !providerRow.providerEnabled) }
                                                 KeyboardAction { anchors.fill: parent; pointerEnabled: false; focusEnabled: true; accessibleName: (providerRow.providerEnabled ? "Disable " : "Enable ") + (providerRow.modelData.name || providerRow.providerId)
                                                     onTriggered: root.toggleProvider(root.selectedLanguage, providerRow.providerId, !providerRow.providerEnabled) } }
                                             Item { id: upControl; width: 28; height: 28
                                                 Text { anchors.centerIn: parent; text: "↑"; color: providerRow.canMoveUp ? theme.inkDim : theme.inkDimmer; font.pixelSize: 16 }
                                                 MouseArea { anchors.fill: parent; enabled: providerRow.canMoveUp; onClicked: root.moveProviderUp(root.selectedLanguage, providerRow.providerId) }
                                                 KeyboardAction { anchors.fill: parent; pointerEnabled: false; focusEnabled: providerRow.canMoveUp; enabled: providerRow.canMoveUp; accessibleName: "Move " + (providerRow.modelData.name || providerRow.providerId) + " up"
                                                     onTriggered: root.moveProviderUp(root.selectedLanguage, providerRow.providerId) } }
                                             Item { id: downControl; width: 28; height: 28
                                                 Text { anchors.centerIn: parent; text: "↓"; color: providerRow.canMoveDown ? theme.inkDim : theme.inkDimmer; font.pixelSize: 16 }
                                                 MouseArea { anchors.fill: parent; enabled: providerRow.canMoveDown; onClicked: root.moveProviderDown(root.selectedLanguage, providerRow.providerId) }
                                                 KeyboardAction { anchors.fill: parent; pointerEnabled: false; focusEnabled: providerRow.canMoveDown; enabled: providerRow.canMoveDown; accessibleName: "Move " + (providerRow.modelData.name || providerRow.providerId) + " down"
                                                     onTriggered: root.moveProviderDown(root.selectedLanguage, providerRow.providerId) } }
                                        }
                                    }
                                }
                            }
                        }
                        Text { visible: root.providerRows.length === 0; text: "No providers are enabled for this language."; topPadding: 22
                               color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13 }
                    }
                }
            }

            Rectangle {
                visible: root.activeTab === "configuration"
                width: parent.width; height: behaviorColumn.implicitHeight + 44; radius: 18
                color: Qt.rgba(0.04,0.05,0.08,0.68); border.width: 1; border.color: theme.edge
                Column { id: behaviorColumn; width: parent.width; padding: 22; spacing: 12
                    Text { text: "Behavior"; color: theme.ink; font.family: theme.display; font.pixelSize: 22 }
                    Row { width: parent.width; spacing: 14
                        Repeater { model: [
                                { title: "Auto-pick", body: "First available provider in the ranked route" },
                                { title: "Fallback", body: "Same language only" },
                                { title: "Manual choice", body: "Sources shows every matching provider and its rank" }
                            ]
                            delegate: Column { id: behaviorItem; required property var modelData; width: (parent.width - 28) / 3; spacing: 6
                                Rectangle { width: parent.width; height: 1; color: Qt.rgba(1,1,1,0.08) }
                                Text { text: behaviorItem.modelData.title.toUpperCase(); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 1.2 }
                                Text { width: parent.width; text: behaviorItem.modelData.body; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12; wrapMode: Text.WordWrap } }
                        }
                    }
                    Text { width: parent.width; wrapMode: Text.WordWrap
                           text: "Scan quality is descriptive, not scored. The source ladder is limited to the selected language."
                           color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                }
            }
        }
    }

    Row {
        anchors.top: parent.top; anchors.right: parent.right; anchors.topMargin: 24; anchors.rightMargin: theme.margin
        spacing: 22
        Text { text: "⌕"; color: searchInput.interactionActive ? theme.ink : theme.inkDim; font.pixelSize: 17
               KeyboardAction { id: searchInput; anchors.fill: parent; accessibleName: "Search"; focusRadius: 5; onTriggered: root.searchClicked() } }
        Text { text: "—"; color: minInput.interactionActive ? theme.ink : theme.inkDim; font.pixelSize: 17
               KeyboardAction { id: minInput; anchors.fill: parent; accessibleName: "Minimize"; focusRadius: 5; onTriggered: root.minimizeRequested() } }
        Text { text: "⛶"; color: fullInput.interactionActive ? theme.ink : theme.inkDim; font.pixelSize: 17
               KeyboardAction { id: fullInput; anchors.fill: parent; accessibleName: "Toggle fullscreen"; focusRadius: 5; onTriggered: root.fullscreenRequested() } }
        Text { text: "⏻"; color: powerInput.interactionActive ? theme.ink : theme.inkDim; font.pixelSize: 17
               KeyboardAction { id: powerInput; anchors.fill: parent; accessibleName: "Close Colosseum"; focusRadius: 5; onTriggered: root.closeRequested() } }
    }
    BackAction { objectName: "tankoyomiConfigurationBack"; variant: "capsule"; tip: "Back to Extensions"
        anchors.top: parent.top; anchors.left: parent.left; anchors.topMargin: 21; anchors.leftMargin: theme.margin - 10
        onTriggered: root.closeFromPage() }
}
