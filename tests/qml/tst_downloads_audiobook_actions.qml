import QtQuick
import QtTest
import "../../qml" as App

Item {
    width: 1280
    height: 1000

    Component {
        id: downloadsPage
        App.DownloadsPage {
            anchors.fill: parent
            downloadsApi: null
            audiobooksApi: null
        }
    }

    TestCase {
        name: "DownloadsAudiobookActions"
        when: windowShown

        function test_stalled_rows_data() {
            return [
                { tag: "waiting for file list", phase: "file-list", received: 0, total: 0, width: 1280,
                  text: "Stalled while waiting for the release’s file list." },
                { tag: "audio stalled on narrow screen", phase: "audio-data", received: 2097152, total: 4194304,
                  width: 390, text: "Stopped receiving audio data at 2 MB of 4 MB." },
                { tag: "unrelated failure", phase: "", received: 0, total: 0, width: 1280,
                  text: "Audiobook · failed — Drive unavailable" }
            ]
        }

        function test_stalled_rows(data) {
            const page = createTemporaryObject(downloadsPage, parent)
            verify(page)
            page.anchors.fill = null
            page.width = data.width
            const key = "work|author#src:failed"
            page.abActive = { [key]: { state: "failed", title: "Test audiobook", author: "Test Author",
                pairKey: "work|author", attempt: data.phase ? "4" : "", stallPhase: data.phase,
                received: data.received, total: data.total, error: "Drive unavailable" } }
            let requested = null
            page.replaceAudiobookRequested.connect((id, attempt) => requested = [id, attempt])
            const status = findChild(page, "downloadsAudiobookStatus_" + key)
            const action = findChild(page, "downloadsReplaceAudiobook_" + key)
            verify(status)
            verify(action)
            compare(status.text, data.text)
            compare(action.activate(), !!data.phase)
            compare(requested, data.phase ? [key, "4"] : null)
            if (data.width < 480) {
                tryVerify(() => action.mapToItem(status, 0, 0).y >= status.height, 2000,
                    "The narrow layout keeps the action below the stall message")
            }
        }

        function test_completed_actions_data() {
            return [
                { tag: "standalone", key: "work|author#as:edition", bookPath: "", missing: false },
                { tag: "paired", key: "work|author#src:release", bookPath: "paired.epub", missing: false },
                { tag: "bundle", key: "bundle#src:release", bookPath: "", missing: false },
                { tag: "legacy", key: "legacy|author", bookPath: "legacy.epub", missing: false },
                { tag: "missing", key: "missing|author#src:release", bookPath: "missing.epub", missing: true }
            ]
        }

        function test_completed_actions(data) {
            const row = {
                id: data.key, title: "Test audiobook", recordingKey: data.key, bundle: data.tag === "bundle",
                bookPath: data.bookPath, missing: data.missing
            }
            const page = createTemporaryObject(downloadsPage, parent)
            verify(page)
            page.abDone = [row]
            let listened = []
            let opened = []
            page.listenAudiobookRequested.connect(item => listened.push(item))
            page.openAudiobookRequested.connect(item => opened.push(item))
            const listen = findChild(page, "downloadsListenAction_" + data.key)
            const read = findChild(page, "downloadsOpenAudiobookBookAction_" + data.key)
            verify(listen)
            verify(read)
            compare(listen.activate(), !data.missing)
            compare(read.activate(), !data.missing && !!data.bookPath)
            compare(listened.length, data.missing ? 0 : 1)
            compare(opened.length, !data.missing && data.bookPath ? 1 : 0)
            if (listened.length) compare(listened[0].recordingKey, data.key)
            if (opened.length) {
                compare(opened[0].bookPath, data.bookPath)
                compare(opened[0].id, data.key)
            }
        }
    }
}
