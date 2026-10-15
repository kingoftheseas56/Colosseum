import QtQuick
import QtWebEngine

// In-app tracker approval page. The provider's sign-in and consent run here
// instead of the outside browser; the native loopback callback finishes the
// connection, so this view never sees a token or authorization result.
Rectangle {
    id: host

    property string approvalUrl: ""

    color: "#0e1016"
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.08)

    WebEngineView {
        id: web
        objectName: "trackerApprovalWebView"
        anchors.fill: parent
        anchors.margins: 1
        backgroundColor: "#0e1016"
        url: host.approvalUrl.length > 0 ? host.approvalUrl : "about:blank"
        // Sign-in pages that open a new tab stay in this view.
        onNewWindowRequested: function(request) { web.url = request.requestedUrl }
    }
}
