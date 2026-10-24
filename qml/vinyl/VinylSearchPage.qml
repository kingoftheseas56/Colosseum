import QtQuick
import ".."

// The same search surface as Tankoban and Theatre, fed by the existing Vinyl
// session. This adapter owns presentation only; provider IDs and cursors stay
// with the catalogue, and taste choices stay with the recommendation service.
SearchSurface {
    id: page
    required property var session
    required property var worldData
    property bool artistPicker: false
    property var initialState: null
    searchMode: "Vinyl"
    placeholder: artistPicker ? "Search artists…" : "Search artists, albums, songs & playlists…"
    coverAspect: 1
    primaryIcon: Qt.resolvedUrl(artistPicker ? "../../assets/icons/lucide/bookmark-plus.svg" : "../../assets/icons/lucide/chevron-right.svg")
    keepOpenOnActivation: artistPicker
    // Profile teardown may already have rebound the global history owner.
    // Commit on explicit submit/open/back, never while this page is destroyed.
    commitOnDestruction: false
    historyStore: worldData.searchHistoryStore
    searchProvider: provider

    function captureState() {
        return {query: query, catalogue: session.captureCatalogue(),
            expanded: expandedGroups.slice(), y: contentY};
    }

    QtObject {
        id: provider
        property string currentQuery: ""
        readonly property bool showSuggestions: page.artistPicker
        readonly property bool loading: page.artistPicker ? page.worldData.artistSearchLoading
            : page.session.catalogueQuery === currentQuery && Object.keys(page.session.catalogueRequests).length > 0
        readonly property string error: page.artistPicker ? page.worldData.artistSearchError
            : !page.session.source || page.session.source.catalogueSearchVersion !== 1 ? "Music search is unavailable." : ""
        readonly property var groups: {
            if (currentQuery !== page.query.trim()) return [];
            if (page.artistPicker) {
                var selected = page.worldData.pickerDraft;
                var artists = page.worldData.artistOptions(currentQuery).map(function(item) {
                    return {title: item.title, cover: item.cover, round: true,
                        selected: selected.indexOf(item.key) >= 0, subtitle: "Artist", data: item};
                });
                return [{key: "artists", group: "Artists", items: artists,
                    loading: page.worldData.artistSearchLoading, error: page.worldData.artistSearchError,
                    hasMore: page.worldData.artistSearchHasMore}];
            }
            if (!currentQuery || page.session.catalogueQuery !== currentQuery) return [];
            return ["artists", "songs", "albums", "playlists"].map(function(kind) {
                var result = page.session.cataloguePages[kind] || {};
                var rows = (result.items || []).map(function(item) {
                    var record = (result.records || []).find(function(album) { return album.id === (item.albumId || item.id); });
                    return {title: item.title, cover: item.cover || "", round: kind === "artists",
                        subtitle: item.subtitle || (kind === "artists" ? "Artist" : ""),
                        data: Object.assign({}, item, {record: record || null})};
                });
                return {key: kind, group: kind.charAt(0).toUpperCase() + kind.slice(1), items: rows,
                    loading: !!page.session.catalogueRequests[kind], error: result.error || "",
                    hasMore: Object.keys(result.nextCursor || {}).length > 0};
            });
        }
        readonly property var results: groups.reduce(function(all, group) { return all.concat(group.items); }, [])
        function cancel() {
            if (page.artistPicker) return;
            page.session.cancelCatalogueRequests();
            if (page.session.browseOperation === "catalogueSearch") page.session.browsing = false;
        }
        function search(value) {
            currentQuery = value;
            if (page.artistPicker) { page.worldData.setArtistSearchQuery(value); return; }
            if (error) return;
            if (value.length < 2) { page.session.clearCatalogue(); return; }
            if (page.session.catalogueQuery !== value) page.session.searchCatalogue(value, "albums", false);
            page.session.catalogueKinds.forEach(function(kind) {
                var result = page.session.cataloguePages[kind] || {};
                if (!Array.isArray(result.items) || result.error) page.session.requestCataloguePage(kind, false);
            });
        }
        function loadMore(kind) {
            if (page.artistPicker) {
                if (page.worldData.artistSearchError) page.worldData.retryArtistSearch();
                else page.worldData.loadMoreArtistSearch();
                return;
            }
            var result = page.session.cataloguePages[kind] || {};
            page.session.requestCataloguePage(kind, Object.keys(result.nextCursor || {}).length > 0);
        }
    }
    Component.onCompleted: {
        if (initialState && !artistPicker) {
            session.restoreCatalogue(initialState.catalogue);
            provider.currentQuery = initialState.query || "";
            restoreQuery(provider.currentQuery);
            searched = query.length >= 2;
            expandedGroups = initialState.expanded || [];
            Qt.callLater(function() { page.contentY = initialState.y || 0; });
        } else if (artistPicker) {
            provider.currentQuery = worldData.artistSearchText;
            restoreQuery(provider.currentQuery);
        }
    }
}
