// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: BSD-3-Clause
// Qt WebEngine request APIs follow Qt Quick Nano Browser; see NanoBrowser-LICENSE.txt.
// Passkey state handling adapted from example/WebAuthDialog.qml at
// qt/qtwebengine a7a7106bc839974d9172c19707da15c21a8a7126.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtWebEngine
import ".."
import "../account" as Account

Item {
    id: root
    // The active view owns the print picker; these are read-only probe surfaces.
    objectName: browser && browser.visible ? "feriaActiveBrowserRequests" : ""
    readonly property bool printPromptPending: printTicket !== null
    required property var browser
    property bool closingSession: browser ? !!browser.closingSession : false
    property bool active: browser && browser.visible && !closingSession
    signal message(string text)
    property int generation: 0
    property int nextId: 0
    property var pending: []
    property var current: null
    property var printTicket: null
    property string printingPath: ""
    property int authState: -1
    property var authDetails: null
    property string authValidationMessage: ""
    readonly property bool authenticating: current !== null && current.kind === "webauth"
    // Page dialogs (alert, confirm, prompt, leave-page), HTTP and proxy sign-in
    // and file choosers are owned here, not by Qt's stock delegates: a page
    // blocked in alert() must be answerable by closing or leaving the tab, and
    // every prompt follows the same queue, fences and look.
    readonly property bool pageDialog: current !== null && current.kind === "dialog"
    readonly property int pageDialogType: pageDialog ? current.request.type : -1
    readonly property bool signingIn: current !== null && current.kind === "signin"
    property var fileTicket: null
    // Prompts raised by a registered engine view hold a native work ticket
    // bound to the document that asked. Component doubles that are not engine
    // views are unmanaged (-1) and keep only the local generation fence.
    readonly property var runtime: typeof FeriaBrowserPolicy !== "undefined" ? FeriaBrowserPolicy.runtime : null
    function admitWork() {
        return runtime && runtime.hasView(browser) ? runtime.admitWork(browser, "request") : -1
    }
    function workLive(entry) { return entry.work < 0 || runtime.workValid(entry.work) }
    function settleWork(entry) {
        if (entry && entry.work > 0) { runtime.releaseWork(entry.work); entry.work = 0 }
    }
    Theme { id: theme }

    function originLabel(url) {
        var match = String(url || "").match(/^([a-z][a-z0-9+.-]*:)\/\/([^/?#]+)/i)
        // Credentials are not part of an origin and can impersonate another host.
        return match ? match[1].toLowerCase() + "//" + match[2].replace(/^.*@/, "") : qsTr("This page")
    }
    function certificateHostLabel(host) {
        var value = String(host || "")
        return value.indexOf("://") >= 0 ? originLabel(value)
            : value.split(/[/?#]/)[0].replace(/^.*@/, "") || qsTr("This page")
    }
    function permissionLabel(type) {
        var labels = [qsTr("access a device"), qsTr("use your microphone"), qsTr("use your camera"),
            qsTr("use your camera and microphone"), qsTr("share your screen"),
            qsTr("share your screen and audio"), qsTr("lock your pointer"),
            qsTr("send notifications"), qsTr("use your location"),
            qsTr("read and change your clipboard"), qsTr("use installed fonts")]
        return labels[Number(type)] || labels[0]
    }
    function currentSitePermissions() {
        if (!active || !browser.profile || typeof browser.profile.listPermissionsForOrigin !== "function") return []
        return browser.profile.listPermissionsForOrigin(browser.url)
    }
    function resetSitePermissions() {
        var permissions = currentSitePermissions()
        var count = 0
        for (var i = 0; i < permissions.length; ++i) {
            if (permissions[i].isValid) { permissions[i].reset(); ++count }
        }
        message(qsTr("Cleared %1 site permissions for %2.").arg(count).arg(originLabel(browser.url)))
        return count
    }
    function reject(entry) {
        if (!entry) return
        settleWork(entry)
        if (!entry.request) return
        if (entry.kind === "permission") {
            if (entry.request.isValid) entry.request.deny()
        } else if (entry.kind === "certificate") entry.request.selectNone()
        else if (entry.kind === "desktop" || entry.kind === "webauth") entry.request.cancel()
        else if (entry.kind === "dialog" || entry.kind === "signin") entry.request.dialogReject()
    }
    function enqueue(kind, request, origin) {
        var entry = {id: ++nextId, generation: generation, kind: kind, work: -1,
                     request: request, origin: kind === "certificate" ? certificateHostLabel(origin)
                        : kind === "webauth" ? String(origin || originLabel(browser.url))
                        : kind === "proxy" ? String(origin) : originLabel(origin)}
        if (kind === "proxy") { entry.kind = "signin"; entry.proxy = true }
        if (!active) { reject(entry); return }
        entry.work = admitWork()
        // Not admitted: the page that asked is retiring or no longer current.
        if (entry.work === 0) { reject(entry); return }
        pending = pending.concat([entry])
        showNext()
    }
    function showNext() {
        if (current || !active || !pending.length) return
        var next = pending[0]
        pending = pending.slice(1)
        if (!next.request || (next.kind === "webauth" && authFinished(next.request.state))) {
            settleWork(next)
            Qt.callLater(showNext)
            return
        }
        current = next
        if (current) prompt.open()
    }
    function answer(id, allow, sourceKind, sourceRow) {
        var entry = current
        if (!entry || entry.id !== id || entry.generation !== generation || !active) return false
        if (allow && entry.kind === "webauth") return false
        // What was typed is read before the prompt is cleared with its request.
        var typed = dialogInput.text, user = signInUser.text, secret = signInPassword.text
        current = null
        prompt.close()
        // An answer for a document that has since been replaced is a denial.
        if (allow && !workLive(entry)) allow = false
        settleWork(entry)
        if (!allow) reject(entry)
        else if (entry.kind === "permission") {
            if (entry.request.isValid) entry.request.grant()
        } else if (entry.kind === "certificate") {
            if (sourceRow >= 0 && sourceRow < entry.request.certificates.length)
                entry.request.select(sourceRow)
            else entry.request.selectNone()
        } else if (entry.kind === "dialog") {
            entry.request.dialogAccept(typed)
        } else if (entry.kind === "signin") {
            entry.request.dialogAccept(user, secret)
        } else {
            var model = sourceKind === "screen" ? entry.request.screensModel : entry.request.windowsModel
            if (!model || sourceRow < 0 || sourceRow >= model.rowCount()) {
                reject(entry)
            } else if (sourceKind === "screen") entry.request.selectScreen(model.index(sourceRow, 0))
            else entry.request.selectWindow(model.index(sourceRow, 0))
        }
        Qt.callLater(showNext)
        return true
    }
    function authFinished(state) {
        return state === WebEngineWebAuthUxRequest.WebAuthUxState.Completed
            || state === WebEngineWebAuthUxRequest.WebAuthUxState.Cancelled
    }
    function refreshAuthState() {
        if (!current || current.kind !== "webauth" || !current.request) {
            authState = -1; authDetails = null; authValidationMessage = ""
            if (pinInput) pinInput.clear()
            if (confirmPinInput) confirmPinInput.clear()
            return
        }
        var request = current.request
        // These Qt properties are constant in the QML type description; take a
        // fresh snapshot on stateChanged, including retries in the same state.
        authDetails = {pin: request.pinRequest, names: request.userNames,
                       failure: request.requestFailureReason}
        authState = request.state
        authValidationMessage = ""
        pinInput.clear(); confirmPinInput.clear()
        if (authFinished(authState)) {
            settleWork(current)
            current = null
            prompt.close()
            Qt.callLater(showNext)
        }
    }
    function submitAuth(id, action, value, confirmation) {
        var entry = current
        if (!entry || entry.id !== id || entry.generation !== generation || !active || entry.kind !== "webauth"
            || !workLive(entry)) return false
        var request = entry.request
        if (action === "account" && request.state === WebEngineWebAuthUxRequest.WebAuthUxState.SelectAccount) {
            if (request.userNames.indexOf(value) < 0) return false
            request.setSelectedAccount(value)
            return true
        }
        if (action === "pin" && request.state === WebEngineWebAuthUxRequest.WebAuthUxState.CollectPin) {
            var info = request.pinRequest
            var setsPin = info.reason !== WebEngineWebAuthUxRequest.PinEntryReason.Challenge
            if (!value || value.length < info.minPinLength) {
                authValidationMessage = qsTr("Enter a PIN with at least %1 characters.").arg(info.minPinLength)
                return false
            }
            if (setsPin && value !== confirmation) {
                authValidationMessage = qsTr("The PINs do not match.")
                return false
            }
            // Clear secrets before calling Qt: the request can finish synchronously.
            pinInput.clear(); confirmPinInput.clear()
            request.setPin(value)
            return true
        }
        if (action === "retry" && request.state === WebEngineWebAuthUxRequest.WebAuthUxState.RequestFailed) {
            request.retry()
            return true
        }
        return false
    }
    function authDescription() {
        if (!current || !authDetails) return ""
        if (authState === WebEngineWebAuthUxRequest.WebAuthUxState.SelectAccount)
            return qsTr("Choose a passkey for %1.").arg(current.origin)
        if (authState === WebEngineWebAuthUxRequest.WebAuthUxState.CollectPin)
            return authDetails.pin.reason === WebEngineWebAuthUxRequest.PinEntryReason.Challenge
                ? qsTr("Enter the PIN for your security key.") : qsTr("Set a new PIN for your security key.")
        if (authState === WebEngineWebAuthUxRequest.WebAuthUxState.FinishTokenCollection)
            return qsTr("Touch your security key to finish signing in to %1.").arg(current.origin)
        if (authState === WebEngineWebAuthUxRequest.WebAuthUxState.RequestFailed) {
            var failures = [qsTr("The request timed out."), qsTr("This key is not registered."),
                qsTr("This key is already registered."), qsTr("Remove and reconnect your security key to unlock it."),
                qsTr("Your security key is locked and needs to be reset."), qsTr("Reconnect your security key and try again."),
                qsTr("This key cannot store passkeys."), qsTr("This key cannot verify your identity."),
                qsTr("This key does not support the required storage."), qsTr("This key is not compatible with this site."),
                qsTr("Your security key is full."), qsTr("The sign-in request was declined."), qsTr("Sign-in was canceled.")]
            return failures[authDetails.failure] || qsTr("Could not sign in with this passkey.")
        }
        return qsTr("Follow the instructions on your security key to sign in to %1.").arg(current.origin)
    }
    function pinErrorDescription() {
        if (!authDetails || authState !== WebEngineWebAuthUxRequest.WebAuthUxState.CollectPin) return ""
        var errors = ["", qsTr("Verification is locked."), qsTr("The PIN is incorrect."), qsTr("The PIN is too short."),
            qsTr("The PIN contains unsupported characters."), qsTr("Choose a different PIN.")]
        var text = errors[authDetails.pin.error] || ""
        if (text && authDetails.pin.remainingAttempts >= 0)
            text += " " + qsTr("%1 attempts remaining.").arg(authDetails.pin.remainingAttempts)
        return text
    }
    function cancelPending() {
        ++generation
        var entries = pending.slice()
        if (current) entries.unshift(current)
        current = null
        pending = []
        printTicket = null
        var files = fileTicket
        fileTicket = null
        prompt.close()
        printPicker.close()
        filePicker.close()
        folderPicker.close()
        for (var i = 0; i < entries.length; ++i) reject(entries[i])
        if (files) { settleWork(files); files.request.dialogReject() }
    }
    // Page dialog and sign-in fields start from the request, never from the
    // previous prompt.
    function refreshFields() {
        // Read from the entry itself: derived properties settle after this runs.
        var entry = current
        dialogInput.text = entry && entry.kind === "dialog"
            && entry.request.type === JavaScriptDialogRequest.DialogTypePrompt
            ? String(entry.request.defaultText || "") : ""
        signInUser.clear(); signInPassword.clear()
    }
    function promptTitle() {
        if (!current) return ""
        if (authenticating || signingIn) return qsTr("Sign in to %1").arg(current.origin)
        if (pageDialog) return pageDialogType === JavaScriptDialogRequest.DialogTypeBeforeUnload
            ? qsTr("Leave this page?") : qsTr("%1 says").arg(current.origin)
        return current.kind === "certificate" ? qsTr("Choose a certificate")
             : current.kind === "desktop" ? qsTr("Share a screen or window") : qsTr("Site permission")
    }
    function promptText() {
        if (!current) return ""
        if (authenticating) return authDescription()
        if (signingIn) return current.proxy ? qsTr("The proxy %1 needs a user name and password.").arg(current.origin)
                                            : qsTr("%1 needs a user name and password.").arg(current.origin)
        if (pageDialog) return pageDialogType === JavaScriptDialogRequest.DialogTypeBeforeUnload
            ? qsTr("Changes you made may not be saved.") : String(current.request.message || "")
        if (current.kind === "certificate") return qsTr("Choose a certificate to identify yourself to %1.").arg(current.origin)
        if (current.kind === "desktop") return qsTr("Choose what to share with the page at %1.").arg(current.origin)
        return qsTr("Allow %1 to %2?").arg(current.origin).arg(permissionLabel(current.request.permissionType))
    }
    function acceptLabel() {
        if (signingIn) return qsTr("Sign in")
        if (pageDialog) return pageDialogType === JavaScriptDialogRequest.DialogTypeBeforeUnload ? qsTr("Leave") : qsTr("OK")
        return qsTr("Allow")
    }
    // A page asked for files. The system chooser answers; leaving or closing
    // the page withdraws it.
    function fileFilters(types) {
        var patterns = []
        for (var i = 0; i < (types || []).length; ++i)
            if (/^\.[a-z0-9]+$/i.test(String(types[i]))) patterns.push("*" + String(types[i]).toLowerCase())
        return patterns.length ? [qsTr("Suggested files (%1)").arg(patterns.join(" ")), qsTr("All files (*)")]
                               : [qsTr("All files (*)")]
    }
    function chooseFiles(request) {
        request.accepted = true
        if (!active || fileTicket) { request.dialogReject(); return }
        var work = admitWork()
        if (work === 0) { request.dialogReject(); return }
        fileTicket = {generation: generation, request: request, work: work}
        if (request.mode === FileDialogRequest.FileModeUploadFolder) { folderPicker.open(); return }
        filePicker.fileMode = request.mode === FileDialogRequest.FileModeOpenMultiple ? FileDialog.OpenFiles
            : request.mode === FileDialogRequest.FileModeSave ? FileDialog.SaveFile : FileDialog.OpenFile
        filePicker.nameFilters = fileFilters(request.acceptedMimeTypes)
        filePicker.open()
    }
    function finishFiles(urls) {
        var ticket = fileTicket
        fileTicket = null
        if (!ticket) return false
        var live = active && ticket.generation === generation && workLive(ticket)
        settleWork(ticket)
        if (!live || !urls || !urls.length) { ticket.request.dialogReject(); return false }
        ticket.request.dialogAccept(urls.map(localPath))
        return true
    }
    function rejectCertificate(error) {
        error.rejectCertificate()
        message(qsTr("Could not securely connect to %1. The site's certificate is not trusted.").arg(originLabel(error.url)))
    }
    function localPath(url) {
        var path = decodeURIComponent(String(url)).replace(/^file:\/\//i, "")
        if (/^\/[a-z]:\//i.test(path)) path = path.slice(1)
        else if (path.length && path[0] !== "/") path = "//" + path
        return path
    }
    function requestPrint() {
        if (!active || printTicket || printingPath.length) return
        printTicket = {generation: generation, browser: browser, origin: originLabel(browser.url)}
        printPicker.open()
    }
    function printPage() { requestPrint() }
    function savePrint(url) {
        var ticket = printTicket
        printTicket = null
        if (!ticket || !active || ticket.generation !== generation || ticket.browser !== browser) return false
        printingPath = localPath(url)
        browser.printToPdf(printingPath)
        return true
    }
    onActiveChanged: if (!active) cancelPending()
    onBrowserChanged: cancelPending()
    onCurrentChanged: { refreshAuthState(); refreshFields() }
    Component.onDestruction: cancelPending()
    Connections {
        target: root.browser
        ignoreUnknownSignals: true
        function onPermissionRequested(permission) {
            if (permission.isValid) root.enqueue("permission", permission, permission.origin)
        }
        function onDesktopMediaRequested(request) { root.enqueue("desktop", request, root.browser.url) }
        function onSelectClientCertificate(selection) { root.enqueue("certificate", selection, selection.host) }
        function onWebAuthUxRequested(request) { root.enqueue("webauth", request, request.relyingPartyId) }
        function onCertificateError(error) { root.rejectCertificate(error) }
        // Accepting the request here keeps Qt's stock delegate from opening.
        // A hidden or closing page is refused by enqueue, as before.
        function onAuthenticationDialogRequested(request) {
            request.accepted = true
            if (request.type === AuthenticationDialogRequest.AuthenticationTypeProxy)
                root.enqueue("proxy", request, request.proxyHost)
            else root.enqueue("signin", request, request.url)
        }
        function onJavaScriptDialogRequested(request) {
            // Retained close/navigation unload consent has one exact-origin
            // owner, including hidden pages. Other dialog policies stay intact.
            if (root.browser && root.browser.managedBeforeUnload === true
                && request.type === JavaScriptDialogRequest.DialogTypeBeforeUnload) return
            request.accepted = true
            root.enqueue("dialog", request, request.securityOrigin)
        }
        function onFileDialogRequested(request) { root.chooseFiles(request) }
        function onNavigationRequested(request) { if (request.isMainFrame) root.cancelPending() }
        function onUrlChanged() { root.cancelPending() }
        function onWindowCloseRequested() { root.cancelPending() }
        function onPrintRequested() { root.requestPrint() }
        function onPdfPrintingFinished(path, success) {
            if (path !== root.printingPath) return
            root.printingPath = ""
            root.message(success ? qsTr("PDF saved.") : qsTr("Could not save this page as a PDF."))
        }
    }
    Connections {
        target: root.current && root.current.kind === "webauth" ? root.current.request : null
        ignoreUnknownSignals: true
        function onStateChanged() { root.refreshAuthState() }
    }
    FileDialog {
        id: printPicker
        title: root.printTicket ? qsTr("Save page from %1 as PDF").arg(root.printTicket.origin || root.originLabel(root.browser.url)) : qsTr("Save page as PDF")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("PDF files (*.pdf)")]
        defaultSuffix: "pdf"
        onAccepted: root.savePrint(selectedFile)
        onRejected: root.printTicket = null
    }
    component PromptField: TextField {
        id: promptField
        implicitHeight: 40
        leftPadding: 12; rightPadding: 12; topPadding: 0; bottomPadding: 0
        verticalAlignment: TextInput.AlignVCenter
        color: theme.ink
        placeholderTextColor: theme.inkDimmer
        font.family: theme.ui; font.pixelSize: 14
        selectByMouse: true
        Accessible.name: placeholderText
        background: Rectangle {
            radius: 10
            color: Qt.rgba(0.015, 0.018, 0.03, 0.68)
            border.width: promptField.activeFocus ? 2 : 1
            border.color: promptField.activeFocus ? theme.gold : theme.edge
        }
    }
    FileDialog {
        id: filePicker
        title: qsTr("Choose a file for %1").arg(root.originLabel(root.browser ? root.browser.url : ""))
        onAccepted: root.finishFiles(fileMode === FileDialog.OpenFiles ? selectedFiles : [selectedFile])
        onRejected: root.finishFiles([])
    }
    FolderDialog {
        id: folderPicker
        title: qsTr("Choose a folder for %1").arg(root.originLabel(root.browser ? root.browser.url : ""))
        onAccepted: root.finishFiles([selectedFolder])
        onRejected: root.finishFiles([])
    }
    Dialog {
        id: prompt
        objectName: "feriaBrowserPermissionDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(500, parent ? parent.width - 32 : 500)
        modal: true
        closePolicy: Popup.CloseOnEscape
        title: root.promptTitle()
        background: Rectangle { color: "#202027"; radius: 16; border.color: theme.edge }
        header: Text {
            text: prompt.title; textFormat: Text.PlainText
            color: theme.ink; font.family: theme.ui; font.pixelSize: 18
            padding: 18; wrapMode: Text.Wrap
        }
        onRejected: if (root.current) root.answer(root.current.id, false)
        onOpened: {
            if (dialogInput.visible) { dialogInput.forceActiveFocus(); dialogInput.selectAll() }
            else if (signInUser.visible) signInUser.forceActiveFocus()
        }
        contentItem: ColumnLayout {
            spacing: 14
            Text {
                objectName: "feriaRequestMessage"
                Layout.fillWidth: true
                Layout.maximumHeight: 260
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                elide: Text.ElideRight
                color: theme.ink; font.family: theme.ui; font.pixelSize: 14
                text: root.promptText()
            }
            PromptField {
                id: dialogInput
                objectName: "feriaDialogInput"
                Accessible.name: qsTr("Answer to the page's question")
                Layout.fillWidth: true
                visible: root.pageDialog && root.pageDialogType === JavaScriptDialogRequest.DialogTypePrompt
                onAccepted: if (root.current) root.answer(root.current.id, true)
            }
            PromptField {
                id: signInUser
                objectName: "feriaSignInUser"
                Layout.fillWidth: true
                visible: root.signingIn
                placeholderText: qsTr("User name")
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                onAccepted: signInPassword.forceActiveFocus()
            }
            PromptField {
                id: signInPassword
                objectName: "feriaSignInPassword"
                Layout.fillWidth: true
                visible: root.signingIn
                placeholderText: qsTr("Password")
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                onAccepted: if (root.current) root.answer(root.current.id, true)
            }
            Repeater {
                model: root.current && root.current.kind === "certificate" ? root.current.request.certificates : []
                delegate: Account.AccountButton {
                    id: certificateButton
                    required property int index
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.subject + " — " + modelData.issuer
                    contentItem: Text {
                        text: certificateButton.text; textFormat: Text.PlainText
                        color: theme.ink; font.family: theme.ui; font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    onClicked: root.answer(root.current.id, true, "certificate", index)
                }
            }
            Repeater {
                model: root.authenticating && root.authDetails && root.authState === WebEngineWebAuthUxRequest.WebAuthUxState.SelectAccount ? root.authDetails.names : []
                delegate: Account.AccountButton {
                    id: passkeyButton
                    required property string modelData
                    Layout.fillWidth: true
                    text: modelData
                    contentItem: Text {
                        text: passkeyButton.text; textFormat: Text.PlainText
                        color: theme.ink; font.family: theme.ui; font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    onClicked: root.submitAuth(root.current.id, "account", modelData)
                }
            }
            PromptField {
                id: pinInput
                objectName: "feriaPasskeyPin"
                Layout.fillWidth: true
                visible: root.authenticating && root.authState === WebEngineWebAuthUxRequest.WebAuthUxState.CollectPin
                placeholderText: qsTr("PIN")
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                onAccepted: if (root.current) root.submitAuth(root.current.id, "pin", text, confirmPinInput.text)
            }
            PromptField {
                id: confirmPinInput
                objectName: "feriaPasskeyConfirmPin"
                Layout.fillWidth: true
                visible: pinInput.visible && root.authDetails && root.authDetails.pin.reason !== WebEngineWebAuthUxRequest.PinEntryReason.Challenge
                placeholderText: qsTr("Confirm PIN")
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                onAccepted: if (root.current) root.submitAuth(root.current.id, "pin", pinInput.text, text)
            }
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.authValidationMessage || root.pinErrorDescription()
                color: theme.inkDim; font.family: theme.ui
                wrapMode: Text.Wrap; textFormat: Text.PlainText
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(300, sources.implicitHeight)
                visible: root.current && root.current.kind === "desktop"
                Column {
                    id: sources
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: root.current && root.current.kind === "desktop" ? root.current.request.screensModel : null
                        delegate: Item {
                            required property int index
                            required property string display
                            width: sources.width
                            height: screenButton.implicitHeight
                            Account.AccountButton {
                                id: screenButton
                                width: parent.width
                                text: qsTr("Screen: %1").arg(parent.display)
                                contentItem: Text {
                                    text: screenButton.text; textFormat: Text.PlainText
                                    color: theme.ink; font.family: theme.ui; font.pixelSize: 13
                                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                                onClicked: root.answer(root.current.id, true, "screen", parent.index)
                            }
                        }
                    }
                    Repeater {
                        model: root.current && root.current.kind === "desktop" ? root.current.request.windowsModel : null
                        delegate: Item {
                            required property int index
                            required property string display
                            width: sources.width
                            height: windowButton.implicitHeight
                            Account.AccountButton {
                                id: windowButton
                                width: parent.width
                                text: qsTr("Window: %1").arg(parent.display)
                                contentItem: Text {
                                    text: windowButton.text; textFormat: Text.PlainText
                                    color: theme.ink; font.family: theme.ui; font.pixelSize: 13
                                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                                onClicked: root.answer(root.current.id, true, "window", parent.index)
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Account.AccountButton {
                    Layout.fillWidth: true; implicitWidth: 100
                    visible: pinInput.visible
                    variant: "primary"; text: qsTr("Continue")
                    onClicked: if (root.current) root.submitAuth(root.current.id, "pin", pinInput.text, confirmPinInput.text)
                }
                Account.AccountButton {
                    Layout.fillWidth: true; implicitWidth: 100
                    visible: root.authenticating && root.authState === WebEngineWebAuthUxRequest.WebAuthUxState.RequestFailed
                    text: qsTr("Try again")
                    onClicked: if (root.current) root.submitAuth(root.current.id, "retry")
                }
                Account.AccountButton {
                    objectName: "feriaRequestCancel"
                    Layout.fillWidth: true; implicitWidth: 100
                    // An alert has nothing to refuse.
                    visible: !(root.pageDialog && root.pageDialogType === JavaScriptDialogRequest.DialogTypeAlert)
                    text: root.pageDialog && root.pageDialogType === JavaScriptDialogRequest.DialogTypeBeforeUnload
                        ? qsTr("Stay") : qsTr("Cancel")
                    onClicked: if (root.current) root.answer(root.current.id, false)
                }
                Account.AccountButton {
                    objectName: "feriaRequestAccept"
                    Layout.fillWidth: true; implicitWidth: 100
                    visible: root.current && (root.current.kind === "permission" || root.pageDialog || root.signingIn)
                    variant: "primary"; text: root.acceptLabel()
                    onClicked: if (root.current) root.answer(root.current.id, true)
                }
            }
        }
    }
}
