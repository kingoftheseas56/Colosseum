import QtQuick
import QtTest
import "../../qml/feria" as Feria

Rectangle {
    id: stage
    width: 500; height: 90
    color: "#08090d"
    Feria.FeriaSiteIdentity {
        id: identity
        anchors.centerIn: parent
        width: 240; height: 36
    }
    TestCase {
        name: "FeriaSiteIdentity"
        when: windowShown
        function test_connectionDisplay_data() {
            return [
                {tag: "https-loaded", url: "https://example.org/account?secret=hidden", loading: false, loaded: true,
                    origin: "https://example.org", status: "HTTPS connection"},
                {tag: "https-connecting", url: "https://login.example.org/", loading: true, loaded: false,
                    origin: "https://login.example.org", status: "Connecting…"},
                {tag: "https-failed", url: "https://expired.example.org/", loading: false, loaded: false,
                    origin: "https://expired.example.org", status: "Connection unverified"},
                {tag: "http-warning", url: "http://example.org:8080/", loading: false, loaded: true,
                    origin: "http://example.org:8080", status: "Not encrypted (HTTP)"},
                {tag: "userinfo-not-provider", url: "https://netflix.com:password@other.example/", loading: false, loaded: true,
                    origin: "https://other.example", status: "HTTPS connection"},
                {tag: "blank", url: "about:blank", loading: false, loaded: true,
                    origin: "New page", status: "No site connection"},
                {tag: "blob", url: "blob:https://example.org/object", loading: false, loaded: true,
                    origin: "https://example.org", status: "Page content"}
            ]
        }
        function test_connectionDisplay(row) {
            identity.currentUrl = row.url
            identity.loading = row.loading
            identity.documentLoaded = row.loaded
            var origin = findChild(identity, "feriaSiteOrigin")
            var status = findChild(identity, "feriaConnectionStatus")
            verify(origin !== null && status !== null, "The current origin and connection status must be visible without opening an address panel")
            verify(origin.visible && status.visible)
            compare(origin.text, row.origin)
            compare(status.text, row.status)
        }
        function test_narrowOriginKeepsDomainSuffix() {
            identity.width = 120
            identity.currentUrl = "https://a-long-subdomain.actual-provider.example/"
            identity.loading = false
            identity.documentLoaded = true
            var origin = findChild(identity, "feriaSiteOrigin")
            verify(origin !== null)
            verify(origin.truncated, "Long origins must stay within the compact browser bar")
            compare(origin.elide, Text.ElideLeft, "Keep the real hostname suffix visible")
            verify(waitForRendering(identity, 3000))
            // QuickTest's save method returns void and throws on write failure.
            grabImage(stage).save(Fixture.evidenceRoot + "/site-identity-narrow.png")
            identity.width = 240
        }
    }
}
