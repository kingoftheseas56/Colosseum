import QtQuick
import "BiblioApi.js" as BiblioApi
import "AudiobookShelfApi.js" as AudiobookShelfApi

SearchSurface {
    id: biblio

    property bool audioFirst: false
    property var audiobookCatalog: null
    property var audioSearchDispatcher: function(query, done) {
        return AudiobookShelfApi.search(biblio.audiobookCatalog, query, done)
    }
    readonly property int bookResultCount: bookLane.rows.length
    readonly property int audioResultCount: audioLane.rows.length
    readonly property bool audioSearching: audioLane.loading
    readonly property var audioResults: audioLane.rows

    signal bookRequested(var book)
    signal audiobookRequested(string workId)
    property Item returnFocusItem: null

    searchMode: "Biblio"
    chromeObjectNamePrefix: "biblioSearch"
    searchInputObjectName: "biblioSearchInput"
    placeholder: audioFirst ? qsTr("Search audiobooks and books…") : qsTr("Search books and audiobooks…")
    searchDispatcher: BiblioApi.search
    searchProvider: provider
    activateTopOnEnter: true
    showSearchErrorSummary: false
    topResult: (audioFirst ? audioLane.loading : bookLane.loading) ? null
        : results.length ? results[0] : null

    readonly property var genreTerms: [
        { label: qsTr("Fiction"), query: "fiction" },
        { label: qsTr("Mystery & Thriller"), query: "thriller" },
        { label: qsTr("Romance"), query: "romance" },
        { label: qsTr("Sci-Fi & Fantasy"), query: "science fiction" },
        { label: qsTr("Biography"), query: "biography" },
        { label: qsTr("History"), query: "history" },
        { label: qsTr("Nonfiction"), query: "nonfiction" },
        { label: qsTr("Young Adult"), query: "young adult" }
    ]
    genres: genreTerms.map(function(genre) { return genre.label })
    genreSearchQuery: function(label) {
        return biblio.genreTerms.find(function(genre) { return genre.label === label }).query
    }

    onQueryChanged: {
        if (query.trim() !== provider.currentQuery) {
            bookLane.reset()
            audioLane.reset()
        }
    }
    onEnabledChanged: {
        if (!enabled || !visible) return
        if (returnFocusItem && returnFocusItem.visible && returnFocusItem.enabled)
            returnFocusItem.forceActiveFocus()
        else
            focusQuery()
    }
    onItemRequested: function(data) {
        returnFocusItem = biblio.Window.activeFocusItem
        if (data.kind === "audiobook")
            biblio.audiobookRequested(data.id)
        else
            biblio.bookRequested(data.book)
    }

    component ProviderLane: QtObject {
        id: lane
        property var rows: []
        property bool loading: false
        property string error: ""
        property var cancelRequest: null
        property int generation: 0

        function cancel() {
            ++generation
            const request = cancelRequest
            cancelRequest = null
            loading = false
            biblio.cancelHandle(request)
        }

        function reset() {
            rows = []
            error = ""
        }

        function run(query, dispatch) {
            cancel()
            error = ""
            loading = true
            const token = generation
            const request = dispatch(query, function(rows, error) {
                if (token !== lane.generation || query !== biblio.query.trim()) return
                lane.rows = rows || []
                lane.error = error || ""
                lane.cancelRequest = null
                lane.loading = false
            })
            if (token === generation && loading) cancelRequest = request
        }
    }

    function searchGroup(key, label, lane, isAudio) {
        return {
            key: key, group: label, loading: lane.loading, error: lane.error,
            emptyText: isAudio ? qsTr("No audiobook matches") : qsTr("No book matches"),
            items: lane.rows.filter(function(row) { return row && (!isAudio || row.workId) }).map(function(row) {
                return {
                    title: row.title,
                    subtitle: row.author,
                    meta: [label, isAudio ? row.author : row.genreLine || row.author].filter(Boolean).join(" · "),
                    synopsis: row.synopsis,
                    cover: row.cover,
                    squareCover: isAudio,
                    data: isAudio ? { kind: "audiobook", id: row.workId }
                                  : { kind: "book", id: row.id || row.title, book: row }
                }
            })
        }
    }

    ProviderLane { id: bookLane }
    ProviderLane { id: audioLane }

    QtObject {
        id: provider
        property string currentQuery: ""
        readonly property bool showSuggestions: false
        readonly property bool loading: bookLane.loading || audioLane.loading
        readonly property string error: bookLane.error || audioLane.error
        readonly property var bookGroup: biblio.searchGroup("books", qsTr("Books"), bookLane, false)
        readonly property var audioGroup: biblio.searchGroup("audiobooks", qsTr("Audiobooks"), audioLane, true)
        readonly property var groups: biblio.audioFirst ? [audioGroup, bookGroup] : [bookGroup, audioGroup]
        readonly property var results: groups[0].items.concat(groups[1].items)

        function cancel() {
            bookLane.cancel()
            audioLane.cancel()
        }

        function search(query) {
            currentQuery = query
            bookLane.reset()
            audioLane.reset()
            if (query.length < 2) return
            bookLane.run(query, biblio.searchDispatcher)
            audioLane.run(query, biblio.audioSearchDispatcher)
        }

        function loadMore(key) {
            if (currentQuery.length < 2 || currentQuery !== biblio.query.trim()) return
            if (key === "books") bookLane.run(currentQuery, biblio.searchDispatcher)
            else if (key === "audiobooks") audioLane.run(currentQuery, biblio.audioSearchDispatcher)
        }
    }
}
