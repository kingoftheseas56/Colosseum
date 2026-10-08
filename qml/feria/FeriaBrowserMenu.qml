import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".." as Colosseum
import "../account" as AccountUi

Dialog {
    id: root
    objectName: "feriaBrowserMenu"
    required property var deck
    required property var accountStore
    property string providerId: ""
    property string currentTitle: ""
    property string currentUrl: ""
    property string section: "tabs"
    property var tabRows: []
    property var bookmarkRows: []
    property var historyRows: []
    property var preferences: ({})
    property int savedTabCount: 0
    property string statusText: ""
    readonly property bool privateBrowsing: !!(deck && deck.activeEntry
        && deck.activeEntry.descriptor && deck.activeEntry.descriptor.privateBrowsing)
    readonly property bool currentBookmarked: {
        for (var row of bookmarkRows) if (String(row.url) === currentUrl) return true
        return false
    }
    signal navigateRequested(string url)
    signal focusAddressRequested()
    signal printRequested()
    signal downloadsRequested()
    signal message(string text)

    width: Math.min(680, Math.max(0, parent ? parent.width - 32 : 680))
    height: Math.min(600, Math.max(0, parent ? parent.height - 32 : 600))
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 20
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Colosseum.Theme { id: theme }

    // These are presentation projections; SessionStore and FeriaAccount own the state.
    function refreshRows() {
        tabRows = deck && deck.sessions ? deck.sessions.browsers() : []
        bookmarkRows = accountStore ? accountStore.browserBookmarks || [] : []
        historyRows = accountStore ? accountStore.browserHistory || [] : []
        preferences = accountStore ? accountStore.browserPreferences || ({}) : ({})
        savedTabCount = deck ? deck.restorableTabCount : 0
    }
    function openSection(nextSection) {
        section = ["tabs", "bookmarks", "history", "settings"].indexOf(nextSection) >= 0 ? nextSection : "tabs"
        statusText = ""
        refreshRows()
        open()
    }
    function notify(text) { statusText = text; message(text) }
    function newTab(isPrivate) {
        var sourceId = deck.sessions.activeId
        close()
        deck.newSpace(sourceId, isPrivate, true)
    }
    function switchTab(id) { close(); deck.sessions.switchTo(id) }
    function closeTab(id) { deck.closeTab(id); refreshRows() }
    function navigate(url) { close(); navigateRequested(String(url)) }
    function setBookmark(url, title, saved) {
        if (accountStore.setBrowserBookmark(url, title, saved) === false) {
            notify("Couldn't update this bookmark.")
            return
        }
        refreshRows()
        notify(saved ? "Bookmark saved" : "Bookmark removed")
    }
    function setPreference(key, value) {
        if (accountStore.setBrowserPreference(key, value) === false)
            notify("Couldn't save this setting.")
        refreshRows()
    }
    onOpened: tabsSection.forceActiveFocus()
    onCurrentUrlChanged: if (visible) close()
    onProviderIdChanged: if (visible) close()
    onAccountStoreChanged: if (visible) close()
    Connections {
        target: root.deck ? root.deck.sessions : null
        function onChanged() { root.refreshRows() }
        function onActiveChangedProp() { root.close() }
    }
    Connections {
        target: root.accountStore
        function onChanged() { root.refreshRows() }
        function onProfileChanged() { root.close(); root.refreshRows() }
    }
    Connections {
        target: root.deck
        function onQuiescingChanged() { if (root.deck.quiescing) root.close() }
        function onRestorableTabCountChanged() { root.refreshRows() }
    }
    background: Rectangle {
        color: theme.biblioWashTop
        radius: 18
        border.width: 1
        border.color: theme.edge
    }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.48) }

    component HubButton: AccountUi.AccountButton {
        id: hubButton
        implicitWidth: Math.max(76, contentItem.implicitWidth + 24)
        implicitHeight: 36
        Accessible.name: text
        contentItem: Text {
            text: hubButton.text
            textFormat: Text.PlainText
            color: hubButton.variant === "primary" ? hubButton.primaryInk
                : (hubButton.enabled ? theme.ink : theme.inkDimmer)
            opacity: hubButton.enabled ? 1.0 : 0.58
            font.family: theme.ui
            font.pixelSize: 13
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    component HubLabel: Text {
        textFormat: Text.PlainText
        color: theme.inkDim
        font.family: theme.ui
        font.pixelSize: 12
        elide: Text.ElideRight
    }
    contentItem: ColumnLayout {
        Accessible.name: "Browser tools"
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    Layout.fillWidth: true
                    text: "Browser"
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: 26
                    font.weight: Font.DemiBold
                }
                HubLabel {
                    Layout.fillWidth: true
                    text: root.currentTitle || root.providerId || "New tab"
                    color: theme.ink
                    font.pixelSize: 13
                }
                HubLabel { Layout.fillWidth: true; text: root.currentUrl || "about:blank" }
            }
            HubLabel {
                objectName: "feriaBrowserPrivacy"
                text: root.privateBrowsing ? "Private tab" : "Regular tab"
                color: root.privateBrowsing ? theme.gold : theme.inkDimmer
                Accessible.name: text
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            HubButton {
                objectName: "feriaBrowserAddress"
                text: "Go to address"
                Layout.fillWidth: true
                onClicked: { root.close(); root.focusAddressRequested() }
            }
            HubButton {
                objectName: "feriaBrowserPrint"
                text: "Print / PDF"
                Layout.fillWidth: true
                onClicked: { root.close(); root.printRequested() }
            }
            HubButton {
                objectName: "feriaBrowserDownloads"
                text: "Downloads"
                Layout.fillWidth: true
                onClicked: { root.close(); root.downloadsRequested() }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            HubButton {
                id: tabsSection
                objectName: "feriaBrowserTabsSection"
                text: "Tabs"
                Layout.fillWidth: true
                variant: root.section === "tabs" ? "primary" : "link"
                onClicked: root.section = "tabs"
            }
            HubButton {
                objectName: "feriaBrowserBookmarksSection"
                text: "Bookmarks"
                Layout.fillWidth: true
                variant: root.section === "bookmarks" ? "primary" : "link"
                onClicked: root.section = "bookmarks"
            }
            HubButton {
                objectName: "feriaBrowserHistorySection"
                text: "History"
                Layout.fillWidth: true
                variant: root.section === "history" ? "primary" : "link"
                onClicked: root.section = "history"
            }
            HubButton {
                objectName: "feriaBrowserSettingsSection"
                text: "Settings"
                Layout.fillWidth: true
                variant: root.section === "settings" ? "primary" : "link"
                onClicked: root.section = "settings"
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: theme.edge }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: ["tabs", "bookmarks", "history", "settings"].indexOf(root.section)
            ColumnLayout {
                spacing: 10
                RowLayout {
                    HubLabel { Layout.fillWidth: true; text: root.tabRows.length + " open tabs" }
                    HubButton { objectName: "feriaBrowserNewTab"; text: "New tab"; onClicked: root.newTab(false) }
                    HubButton { objectName: "feriaBrowserNewPrivate"; text: "New private tab"; onClicked: root.newTab(true) }
                }
                ListView {
                    objectName: "feriaBrowserTabsList"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: root.tabRows
                    ScrollBar.vertical: ScrollBar {}
                    delegate: RowLayout {
                        required property var modelData
                        width: ListView.view.width - 14
                        height: 58
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            HubLabel { Layout.fillWidth: true; color: theme.ink; text: modelData.title || modelData.target.title || "New tab"; font.pixelSize: 13 }
                            HubLabel { Layout.fillWidth: true; text: (modelData.target.privateBrowsing ? "Private · " : "") + (modelData.target.url || "about:blank") }
                        }
                        HubButton {
                            objectName: "feriaBrowserSwitch_" + modelData.id
                            text: root.deck.sessions.activeId === modelData.id ? "Current" : "Switch"
                            variant: "link"
                            onClicked: root.switchTab(modelData.id)
                            Accessible.name: "Switch to " + (modelData.title || "tab")
                        }
                        HubButton {
                            objectName: "feriaBrowserClose_" + modelData.id
                            text: "Close"
                            variant: "link"
                            onClicked: root.closeTab(modelData.id)
                            Accessible.name: "Close " + (modelData.title || "tab")
                        }
                    }
                    HubLabel { anchors.centerIn: parent; visible: !root.tabRows.length; text: "Open a new tab to start browsing." }
                }
            }
            ColumnLayout {
                spacing: 10
                HubButton {
                    objectName: "feriaBrowserBookmarkCurrent"
                    text: root.currentBookmarked ? "Remove current bookmark" : "Bookmark current page"
                    enabled: /^https?:\/\//i.test(root.currentUrl)
                    onClicked: root.setBookmark(root.currentUrl, root.currentTitle || root.currentUrl, !root.currentBookmarked)
                }
                ListView {
                    objectName: "feriaBrowserBookmarksList"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: root.bookmarkRows
                    ScrollBar.vertical: ScrollBar {}
                    delegate: RowLayout {
                        required property var modelData
                        required property int index
                        width: ListView.view.width - 14
                        height: 58
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            HubButton {
                                objectName: "feriaBrowserBookmark_" + index
                                Layout.fillWidth: true
                                text: modelData.title || modelData.url
                                variant: "link"
                                onClicked: root.navigate(modelData.url)
                            }
                            HubLabel { Layout.fillWidth: true; text: modelData.url }
                        }
                        HubButton {
                            objectName: "feriaBrowserBookmarkRemove_" + index
                            text: "Remove"
                            variant: "link"
                            onClicked: root.setBookmark(modelData.url, modelData.title || modelData.url, false)
                            Accessible.name: "Remove bookmark " + (modelData.title || modelData.url)
                        }
                    }
                    HubLabel { anchors.centerIn: parent; visible: !root.bookmarkRows.length; text: "Save a page to find it here later." }
                }
            }
            ColumnLayout {
                spacing: 10
                RowLayout {
                    HubLabel { Layout.fillWidth: true; text: "Visited pages" }
                    HubButton {
                        objectName: "feriaBrowserClearHistory"
                        text: "Clear history"
                        enabled: root.historyRows.length > 0
                        onClicked: { root.accountStore.clearBrowserHistory(); root.refreshRows(); root.notify("Browsing history cleared") }
                    }
                }
                ListView {
                    objectName: "feriaBrowserHistoryList"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: root.historyRows
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ColumnLayout {
                        required property var modelData
                        required property int index
                        width: ListView.view.width - 14
                        height: 58
                        spacing: 0
                        HubButton {
                            objectName: "feriaBrowserHistory_" + index
                            Layout.fillWidth: true
                            text: modelData.title || modelData.url
                            variant: "link"
                            onClicked: root.navigate(modelData.url)
                        }
                        HubLabel { Layout.fillWidth: true; text: modelData.url }
                    }
                    HubLabel { anchors.centerIn: parent; visible: !root.historyRows.length; text: "Pages visited in regular tabs appear here." }
                }
            }
            ColumnLayout {
                spacing: 12
                CheckBox {
                    id: restorePreference
                    objectName: "feriaBrowserRestoreTabsPreference"
                    text: "Restore regular tabs when Colosseum opens"
                    checked: root.preferences.restoreTabs === true
                    onClicked: root.setPreference("restoreTabs", checked)
                    contentItem: HubLabel { text: restorePreference.text; color: theme.ink; leftPadding: 32; verticalAlignment: Text.AlignVCenter; font.pixelSize: 13 }
                    indicator: Rectangle {
                        x: 0; y: (restorePreference.height - height) / 2
                        width: 22; height: 22; radius: 6
                        color: restorePreference.checked ? theme.gold : theme.glassTint
                        border.color: restorePreference.activeFocus ? theme.gold : theme.edge
                        Text { anchors.centerIn: parent; text: restorePreference.checked ? "✓" : ""; color: theme.biblioWashBottom }
                    }
                }
                CheckBox {
                    id: freezePreference
                    objectName: "feriaBrowserFreezeBackgroundPreference"
                    text: "Freeze inactive tabs when no media is playing"
                    checked: root.preferences.freezeBackground === true
                    onClicked: root.setPreference("freezeBackground", checked)
                    contentItem: HubLabel { text: freezePreference.text; color: theme.ink; leftPadding: 32; verticalAlignment: Text.AlignVCenter; font.pixelSize: 13 }
                    indicator: Rectangle {
                        x: 0; y: (freezePreference.height - height) / 2
                        width: 22; height: 22; radius: 6
                        color: freezePreference.checked ? theme.gold : theme.glassTint
                        border.color: freezePreference.activeFocus ? theme.gold : theme.edge
                        Text { anchors.centerIn: parent; text: freezePreference.checked ? "✓" : ""; color: theme.biblioWashBottom }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    HubLabel { text: "Search engine"; font.pixelSize: 13; color: theme.ink }
                    ComboBox {
                        id: searchEngine
                        objectName: "feriaBrowserSearchEngine"
                        Layout.fillWidth: true
                        model: ["DuckDuckGo", "Google", "Bing"]
                        property var keys: ["duckduckgo", "google", "bing"]
                        currentIndex: Math.max(0, keys.indexOf(root.preferences.searchEngine || "duckduckgo"))
                        onActivated: root.setPreference("searchEngine", keys[currentIndex])
                        Accessible.name: "Search engine"
                        contentItem: HubLabel { text: searchEngine.displayText; color: theme.ink; leftPadding: 10; rightPadding: 32; verticalAlignment: Text.AlignVCenter; font.pixelSize: 13 }
                        indicator: Text {
                            x: searchEngine.width - width - 12
                            y: (searchEngine.height - height) / 2
                            text: "⌄"; color: theme.inkDim; font.pixelSize: 18
                        }
                        background: Rectangle { radius: 8; color: theme.glassTint; border.color: searchEngine.activeFocus ? theme.gold : theme.edge }
                        delegate: ItemDelegate {
                            required property var modelData
                            required property int index
                            width: searchEngine.width
                            text: modelData
                            highlighted: searchEngine.highlightedIndex === index
                            contentItem: HubLabel { text: modelData; color: theme.ink; font.pixelSize: 13 }
                            background: Rectangle { color: parent.highlighted ? theme.glassHi : theme.biblioWashTop }
                        }
                        popup.background: Rectangle { color: theme.biblioWashTop; radius: 8; border.color: theme.edge }
                    }
                }
                HubLabel { Layout.fillWidth: true; text: "Private tabs are excluded from browsing history and tab restore."; wrapMode: Text.WordWrap; elide: Text.ElideNone }
                RowLayout {
                    HubButton {
                        objectName: "feriaBrowserRestorePrevious"
                        text: "Restore previous tabs (" + root.savedTabCount + ")"
                        enabled: root.savedTabCount > 0
                        onClicked: { root.close(); root.deck.restoreTabs() }
                    }
                    HubButton {
                        objectName: "feriaBrowserDismissPrevious"
                        text: "Dismiss previous tabs"
                        enabled: root.savedTabCount > 0
                        variant: "link"
                        onClicked: { root.deck.dismissRestoreTabs(); root.refreshRows() }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            HubLabel { Layout.fillWidth: true; text: root.statusText; Accessible.name: text }
            HubButton { objectName: "feriaBrowserMenuDismiss"; text: "Done"; onClicked: root.close() }
        }
    }
}
