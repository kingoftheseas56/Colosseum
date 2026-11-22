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
