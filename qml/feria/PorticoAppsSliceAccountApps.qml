import QtQuick
import QtQuick.Layouts
import "PorticoData.js" as Data

Item {
    id: root
    objectName: "account-apps-content"
    required property var controller
    readonly property real u: controller.unit
    readonly property real m: controller.marginX
    readonly property var catalogue: Data.CATALOG.watch.concat(Data.CATALOG.listen, Data.CATALOG.read)
    readonly property var others: catalogue.filter(function(k) { return !Data.P[k].app && controller.activeApps.indexOf(k) < 0 })
    readonly property var appOnly: catalogue.filter(function(k) { return Data.P[k].app })
    readonly property var serviceKeys: ["region"].concat(controller.activeApps, others, appOnly)
    readonly property var selected: Data.P[controller.accountApp] || null
    readonly property bool inRow: controller.activeApps.indexOf(controller.accountApp) >= 0
    readonly property var visits: controller.allSessions().filter(function(s) { return s.pk === controller.accountApp })
    readonly property var feeds: Data.SHELVES.filter(function(s) { return s.apps.indexOf(controller.accountApp) >= 0 })

    property string contentFocusArea: ""
    property int serviceFocusIndex: -1
    property int regionFocusIndex: -1
    property int actionFocusIndex: -1
    readonly property bool contentFocusActive: contentFocusArea !== ""

    function select(k) {
        controller.accountApp = k
    }

    function resetContentFocus() {
        contentFocusArea = ""
        serviceFocusIndex = -1
        regionFocusIndex = -1
        actionFocusIndex = -1
    }

    function enterFromShell() {
        if (!visible || serviceKeys.length === 0)
            return false
        setServiceFocus(0)
        return true
    }

    function setServiceFocus(index) {
        if (serviceKeys.length === 0) {
            resetContentFocus()
            return
        }
        contentFocusArea = "service"
        serviceFocusIndex = Math.max(0, Math.min(serviceKeys.length - 1, index))
        regionFocusIndex = -1
        actionFocusIndex = -1
        controller.accountShellFocusIndex = 4
        scheduleEnsureVisible()
    }

    function setRegionFocus(index) {
        contentFocusArea = "region"
        regionFocusIndex = Math.max(0, Math.min(7, index))
        actionFocusIndex = -1
        controller.accountShellFocusIndex = 4
        scheduleEnsureVisible()
    }

    function setActionFocus(index) {
        var labels = actionLabels()
        if (labels.length === 0) {
            contentFocusArea = "service"
            actionFocusIndex = -1
            scheduleEnsureVisible()
            return
        }
        contentFocusArea = "action"
        actionFocusIndex = Math.max(0, Math.min(labels.length - 1, index))
        regionFocusIndex = -1
        controller.accountShellFocusIndex = 4
        scheduleEnsureVisible()
    }

    function serviceKeyAt(index) {
        return index >= 0 && index < serviceKeys.length ? serviceKeys[index] : ""
    }

    function selectedServiceIndex() {
        var i = serviceKeys.indexOf(controller.accountApp)
        return i >= 0 ? i : 0
    }

    function actionLabels() {
        if (controller.accountApp === "region" || !selected || selected.app)
            return []
        var rowActions = inRow ? ["Move earlier", "Move later", "Remove from row"] : ["Add to row"]
        return ["Open provider"].concat(rowActions, ["See all stats"])
    }

    function actionIndexForLabel(label) {
        return actionLabels().indexOf(label)
    }

    function activateAction(index) {
        var labels = actionLabels()
        if (index < 0 || index >= labels.length)
            return false
        var label = labels[index]
        var k = controller.accountApp
        if (label === "Open provider") {
            controller.openHost(k, "", "home")
        } else if (label === "Add to row" || label === "Remove from row") {
            controller.toggleApp(k)
        } else if (label === "See all stats") {
            controller.statsBy = "app"
            controller.accountTab = "stats"
        } else {
            var a = controller.activeApps.slice()
            var i = a.indexOf(k)
            var j = i + (label === "Move earlier" ? -1 : 1)
            if (i >= 0 && j >= 0 && j < a.length) {
                a.splice(i, 1)
                a.splice(j, 0, k)
                controller.activeApps = a
                controller.persistApps()
            }
        }

        if (controller.viewState === "account" && controller.accountTab === "apps") {
            serviceFocusIndex = selectedServiceIndex()
            var nextLabels = actionLabels()
            if (nextLabels.length > 0) {
                contentFocusArea = "action"
                actionFocusIndex = Math.min(index, nextLabels.length - 1)
                scheduleEnsureVisible()
            } else {
                setServiceFocus(serviceFocusIndex)
            }
        }
        return true
    }

    function enterServiceDetail() {
        var key = serviceKeyAt(serviceFocusIndex)
        if (!key)
            return false
        select(key)
        if (key === "region") {
            var regions = ["United States", "United Kingdom", "India", "Canada", "Australia", "Germany", "Japan", "Brazil"]
            var current = regions.indexOf(controller.region)
            setRegionFocus(current >= 0 ? current : 0)
            return true
        }
        if (actionLabels().length > 0) {
            setActionFocus(0)
            return true
        }
        scheduleEnsureVisible()
        return true
    }

    function returnToAppsTab() {
        resetContentFocus()
        controller.accountShellFocusIndex = 4
    }

    function handleKey(key) {
        if (!contentFocusActive)
            return false

        if (contentFocusArea === "service") {
            if (key === Qt.Key_Up) {
                if (serviceFocusIndex === 0)
                    returnToAppsTab()
                else
                    setServiceFocus(serviceFocusIndex - 1)
                return true
            }
            if (key === Qt.Key_Down) {
                setServiceFocus(Math.min(serviceKeys.length - 1, serviceFocusIndex + 1))
                return true
            }
            if (key === Qt.Key_Left) {
                if (serviceFocusIndex === 0)
                    returnToAppsTab()
                return true
            }
            if (key === Qt.Key_Right) {
                enterServiceDetail()
                return true
            }
            if (key === Qt.Key_Return || key === Qt.Key_Enter) {
                var service = serviceKeyAt(serviceFocusIndex)
                if (service)
                    select(service)
                return true
            }
            return false
        }

        if (contentFocusArea === "region") {
            if (key === Qt.Key_Left) {
                if (regionFocusIndex % 2 === 0)
                    setServiceFocus(0)
                else
                    setRegionFocus(regionFocusIndex - 1)
                return true
            }
            if (key === Qt.Key_Right) {
                if (regionFocusIndex % 2 === 0 && regionFocusIndex + 1 < 8)
                    setRegionFocus(regionFocusIndex + 1)
                return true
            }
            if (key === Qt.Key_Up) {
                if (regionFocusIndex < 2)
                    setServiceFocus(0)
                else
                    setRegionFocus(regionFocusIndex - 2)
                return true
            }
            if (key === Qt.Key_Down) {
                if (regionFocusIndex + 2 < 8)
                    setRegionFocus(regionFocusIndex + 2)
                return true
            }
            if (key === Qt.Key_Return || key === Qt.Key_Enter) {
                var regions = ["United States", "United Kingdom", "India", "Canada", "Australia", "Germany", "Japan", "Brazil"]
                controller.setRegion(regions[regionFocusIndex])
                return true
            }
            return false
        }

        if (contentFocusArea === "action") {
            var labels = actionLabels()
            if (labels.length === 0) {
                setServiceFocus(selectedServiceIndex())
                return true
            }
            if (key === Qt.Key_Up || key === Qt.Key_Left) {
                if (actionFocusIndex === 0)
                    setServiceFocus(selectedServiceIndex())
                else
                    setActionFocus(actionFocusIndex - 1)
                return true
            }
            if (key === Qt.Key_Down || key === Qt.Key_Right) {
                setActionFocus(Math.min(labels.length - 1, actionFocusIndex + 1))
                return true
            }
            if (key === Qt.Key_Return || key === Qt.Key_Enter)
                return activateAction(actionFocusIndex)
            return false
        }
        return false
    }

    function serviceItemAt(index) {
        if (index === 0)
            return regionRow
        var offset = index - 1
        if (offset < controller.activeApps.length)
            return activeRepeater.itemAt(offset)
        offset -= controller.activeApps.length
        if (offset < others.length)
            return othersRepeater.itemAt(offset)
        offset -= others.length
        if (offset < appOnly.length)
            return appOnlyRepeater.itemAt(offset)
        return null
    }

    function findNamedChild(item, name) {
        if (!item)
            return null
        if (item.objectName === name)
            return item
        var kids = item.children
        for (var i = 0; i < kids.length; ++i) {
            var found = findNamedChild(kids[i], name)
            if (found)
                return found
        }
        return null
    }

    function focusedItem() {
        if (contentFocusArea === "service")
            return serviceItemAt(serviceFocusIndex)
        if (contentFocusArea === "region")
            return regionRepeater.itemAt(regionFocusIndex)
        if (contentFocusArea === "action")
            return findNamedChild(detail, "account-apps-action-" + actionFocusIndex)
        return null
    }

    function ensureVisible(item) {
        if (!item)
            return
        var p = item.mapToItem(scroller.contentItem, 0, 0)
        var pad = 0.7 * u
        var top = p.y - pad
        var bottom = p.y + item.height + pad
        if (top < scroller.contentY)
            scroller.contentY = Math.max(0, top)
        else if (bottom > scroller.contentY + scroller.height)
            scroller.contentY = Math.min(scroller.contentHeight - scroller.height, bottom - scroller.height)
    }

    function scheduleEnsureVisible() {
        Qt.callLater(function() {
            root.ensureVisible(root.focusedItem())
        })
    }

    Flickable {
        id: scroller
        objectName: "account-apps-scroller"
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, columns.implicitHeight + 9 * u)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: columns
            x: m
            y: 0.8 * u
            width: parent.width - 2 * m
            spacing: 1.3 * u

            Rectangle {
                width: 24 * u
                height: list.implicitHeight + 1.2 * u
                radius: 1.2 * u
                color: Qt.rgba(1,1,1,0.035)
                border.width: 1
                border.color: Qt.rgba(1,1,1,0.1)

                Column {
                    id: list
                    x: 0.6 * u
                    y: 0.6 * u
                    width: parent.width - 1.2 * u
                    spacing: 0.15 * u

                    Text {
                        topPadding: 0.4 * u
                        bottomPadding: 0.4 * u
                        leftPadding: 0.8 * u
                        text: "Settings"
                        color: controller.slate
                        font.family: controller.uiFont
                        font.pixelSize: 0.84 * u
                    }
                    PorticoAppsSliceAccountAppRow {
                        id: regionRow
                        objectName: "account-apps-service-region"
                        width: list.width
                        unit: u
                        selected: controller.accountApp === "region"
                        focusedState: root.contentFocusArea === "service" && root.serviceFocusIndex === 0
                        name: "Catalogue region"
                        detail: controller.region
                        glyph: "region"
                        controller: root.controller
                        onEntered: root.setServiceFocus(0)
                        onTriggered: root.select("region")
                    }

                    Text {
                        topPadding: u
                        bottomPadding: 0.4 * u
                        leftPadding: 0.8 * u
                        text: "In your row"
                        color: controller.slate
                        font.family: controller.uiFont
                        font.pixelSize: 0.84 * u
                    }
                    Repeater {
                        id: activeRepeater
                        model: controller.activeApps
                        delegate: PorticoAppsSliceAccountAppRow {
                            required property int index
                            required property string modelData
                            readonly property int globalServiceIndex: 1 + index
                            objectName: "account-apps-service-" + modelData + "-" + globalServiceIndex
                            width: list.width
                            unit: u
                            selected: controller.accountApp === modelData
                            focusedState: root.contentFocusArea === "service" && root.serviceFocusIndex === globalServiceIndex
                            name: controller.providerName(modelData)
                            detail: "Not signed in"
                            glyph: modelData
                            controller: root.controller
                            onEntered: root.setServiceFocus(globalServiceIndex)
                            onTriggered: root.select(modelData)
                        }
                    }

                    Text {
                        visible: root.others.length > 0
                        topPadding: u
                        bottomPadding: 0.4 * u
                        leftPadding: 0.8 * u
                        text: "Not in your row"
                        color: controller.slate
                        font.family: controller.uiFont
                        font.pixelSize: 0.84 * u
                    }
                    Repeater {
                        id: othersRepeater
                        model: root.others
                        delegate: PorticoAppsSliceAccountAppRow {
                            required property int index
                            required property string modelData
                            readonly property int globalServiceIndex: 1 + controller.activeApps.length + index
                            objectName: "account-apps-service-" + modelData + "-" + globalServiceIndex
                            width: list.width
                            unit: u
                            selected: controller.accountApp === modelData
                            focusedState: root.contentFocusArea === "service" && root.serviceFocusIndex === globalServiceIndex
                            name: controller.providerName(modelData)
                            detail: "Add it to see its shelves"
                            glyph: modelData
                            controller: root.controller
                            onEntered: root.setServiceFocus(globalServiceIndex)
                            onTriggered: root.select(modelData)
                        }
                    }

                    Text {
                        topPadding: u
                        bottomPadding: 0.4 * u
                        leftPadding: 0.8 * u
                        text: "App only"
                        color: controller.slate
                        font.family: controller.uiFont
                        font.pixelSize: 0.84 * u
                    }
                    Repeater {
                        id: appOnlyRepeater
                        model: root.appOnly
                        delegate: PorticoAppsSliceAccountAppRow {
                            required property int index
                            required property string modelData
                            readonly property int globalServiceIndex: 1 + controller.activeApps.length + root.others.length + index
                            objectName: "account-apps-service-" + modelData + "-" + globalServiceIndex
                            width: list.width
                            unit: u
                            selected: controller.accountApp === modelData
                            focusedState: root.contentFocusArea === "service" && root.serviceFocusIndex === globalServiceIndex
                            name: controller.providerName(modelData)
                            detail: "Can't open inside Colosseum"
                            glyph: modelData
                            controller: root.controller
                            onEntered: root.setServiceFocus(globalServiceIndex)
                            onTriggered: root.select(modelData)
                        }
                    }
                }
            }

            Rectangle {
                width: columns.width - 24 * u - 1.3 * u
                height: Math.max(42 * u, detail.implicitHeight + 3.6 * u)
                radius: 1.2 * u
                color: Qt.rgba(1,1,1,0.035)
                border.width: 1
                border.color: Qt.rgba(1,1,1,0.1)

                Column {
                    id: detail
                    x: 1.9 * u
                    y: 1.7 * u
                    width: parent.width - 3.8 * u
                    spacing: 1.4 * u

                    Row {
                        spacing: 1.2 * u
                        Rectangle {
                            width: 3.8 * u
                            height: 3.8 * u
                            radius: 1.1 * u
                            color: Qt.rgba(1,1,1,0.05)
                            border.width: 1
                            border.color: Qt.rgba(1,1,1,0.1)
                            PorticoCombinedGlyph {
                                anchors.centerIn: parent
                                width: 2 * u
                                height: 2 * u
                                glyphKey: controller.accountApp
                                tone: controller.ink
                            }
                        }
                        Column {
                            spacing: 0.2 * u
                            Text {
                                text: controller.accountApp === "region" ? "Catalogue region" : controller.providerName(controller.accountApp)
                                color: controller.ink
                                font.family: controller.displayFont
                                font.pixelSize: 2.3 * u
                            }
                            Text {
                                text: controller.accountApp === "region" ? controller.region
                                     : (root.selected ? (root.selected.app ? "Only in its own app" : root.selected.d + "  ·  Opens inside Colosseum") : "")
                                color: controller.slate
                                font.family: controller.uiFont
                                font.pixelSize: 0.93 * u
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Qt.rgba(1,1,1,0.1)
                    }

                    Text {
                        visible: controller.accountApp === "region"
                        width: parent.width
                        text: "Shelves, charts and the list of services that carry a title depend on region. Portico uses the region you pick here; it doesn't look up where you are."
                        color: controller.mist
                        font.family: controller.uiFont
                        font.pixelSize: 0.95 * u
                        lineHeight: 1.5
                        wrapMode: Text.WordWrap
                    }

                    Grid {
                        visible: controller.accountApp === "region"
                        columns: 2
                        spacing: 0.6 * u
                        Repeater {
                            id: regionRepeater
                            model: ["United States", "United Kingdom", "India", "Canada", "Australia", "Germany", "Japan", "Brazil"]
                            delegate: Rectangle {
                                required property int index
                                required property string modelData
                                property bool focusedState: root.contentFocusArea === "region" && root.regionFocusIndex === index
                                objectName: "account-apps-region-" + index
                                width: (detail.width - 0.6 * u) / 2
                                height: 3 * u
                                radius: 0.7 * u
                                color: focusedState ? Qt.rgba(240/255,196/255,74/255,0.12)
                                                    : controller.region === modelData ? Qt.rgba(240/255,196/255,74/255,0.1)
                                                                                     : Qt.rgba(1,1,1,0.035)
                                border.width: focusedState ? 2 : 1
                                border.color: focusedState || controller.region === modelData ? controller.gold : Qt.rgba(1,1,1,0.1)
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: u
                                    spacing: 0.7 * u
                                    Rectangle {
                                        width: 0.6 * u
                                        height: width
                                        radius: width / 2
                                        color: controller.region === modelData ? controller.gold : "transparent"
                                        border.width: 1
                                        border.color: controller.region === modelData ? controller.gold : controller.slate
                                    }
                                    Text {
                                        text: modelData
                                        color: controller.ink
                                        font.family: controller.uiFont
                                        font.pixelSize: 0.92 * u
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onEntered: root.setRegionFocus(index)
                                    onClicked: {
                                        root.setRegionFocus(index)
                                        controller.setRegion(modelData)
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        visible: !!(root.selected && root.selected.app)
                        width: detail.width
                        height: 11 * u
                        radius: u
                        color: Qt.rgba(1,1,1,0.04)
                        border.width: 1
                        border.color: Qt.rgba(1,1,1,0.1)
                        Column {
                            anchors { fill: parent; margins: 1.4 * u }
                            spacing: 0.8 * u
                            Text {
                                text: "Why it can't open here"
                                color: controller.ink
                                font.family: controller.displayFont
                                font.pixelSize: 1.4 * u
                            }
                            Text {
                                width: parent.width
                                wrapMode: Text.WordWrap
                                text: controller.providerName(controller.accountApp) + " books are read in its own app or device, so Colosseum has no page to open. Title pages can still show availability there."
                                color: controller.mist
                                font.family: controller.uiFont
                                font.pixelSize: 0.95 * u
                            }
                        }
                    }

                    Repeater {
                        model: root.selected && !root.selected.app && controller.accountApp !== "region"
                               ? ["Sign-in", "Your row", "Shelves it feeds", "Your time here"] : []
                        delegate: Rectangle {
                            required property string modelData
                            width: detail.width
                            height: modelData === "Shelves it feeds" ? 8 * u : 10 * u
                            radius: u
                            color: Qt.rgba(1,1,1,0.04)
                            border.width: 1
                            border.color: Qt.rgba(1,1,1,0.1)

                            Column {
                                anchors { fill: parent; margins: 1.4 * u }
                                spacing: 0.6 * u
                                Text {
                                    text: modelData
                                    color: controller.ink
                                    font.family: controller.displayFont
                                    font.pixelSize: 1.4 * u
                                }
                                Text {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    color: controller.mist
                                    font.family: controller.uiFont
                                    font.pixelSize: 0.92 * u
                                    text: modelData === "Sign-in"
                                          ? "Sign in on " + controller.providerName(controller.accountApp) + "'s own page. Colosseum keeps the session; only the service can confirm whether you are signed in."
                                          : modelData === "Your row"
                                            ? (root.inRow ? "Position " + (controller.activeApps.indexOf(controller.accountApp) + 1) + " of " + controller.activeApps.length + "."
                                                          : "Add this service to show its shelves on Home.")
                                            : modelData === "Shelves it feeds"
                                              ? (root.feeds.length ? root.feeds.map(function(s) { return s.title }).join("  ·  ") : "No shelves. It opens at its home page.")
                                              : (root.visits.length ? controller.durationText(controller.totalMins(root.visits)) + " across " + root.visits.length + " visits."
                                                                    : "Nothing recorded here yet.")
                                }

                                Row {
                                    visible: modelData === "Sign-in" || modelData === "Your row" || modelData === "Your time here"
                                    spacing: 0.6 * u
                                    Repeater {
                                        model: modelData === "Sign-in" ? ["Open provider"]
                                             : modelData === "Your row" ? (root.inRow ? ["Move earlier", "Move later", "Remove from row"] : ["Add to row"])
                                             : ["See all stats"]
                                        delegate: Rectangle {
                                            required property string modelData
                                            readonly property int globalActionIndex: root.actionIndexForLabel(modelData)
                                            property bool focusedState: root.contentFocusArea === "action" && root.actionFocusIndex === globalActionIndex
                                            objectName: "account-apps-action-" + globalActionIndex
                                            width: Math.max(6 * u, label.implicitWidth + 2 * u)
                                            height: 2.5 * u
                                            radius: 0.7 * u
                                            color: focusedState ? Qt.rgba(240/255,196/255,74/255,0.12) : Qt.rgba(1,1,1,0.07)
                                            border.width: focusedState ? 2 : 1
                                            border.color: focusedState ? controller.gold : Qt.rgba(1,1,1,0.13)
                                            Text {
                                                id: label
                                                anchors.centerIn: parent
                                                text: modelData
                                                color: controller.ink
                                                font.family: controller.uiFont
                                                font.pixelSize: 0.88 * u
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                onEntered: root.setActionFocus(globalActionIndex)
                                                onClicked: {
                                                    root.setActionFocus(globalActionIndex)
                                                    root.activateAction(globalActionIndex)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
