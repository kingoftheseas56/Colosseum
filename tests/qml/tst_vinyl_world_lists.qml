import QtQuick
import QtTest
import "../fixtures/vinyl-world/Catalogue.js" as Snapshot

TestCase {
    name: "VinylWorldLists"
    property var subject
    property var catalogue
    // Test-side mirror of the people-table canonicalisation, so the checks
    // below are computed from the catalogue and not from the implementation.
    function canon(name) {
        var table = catalogue.people || {}, keys = Object.keys(table)
        if (keys.indexOf(name) >= 0) return name
        function simple(s) { return String(s).toLowerCase().replace(/[^a-z0-9]/g, "") }
        var exact = keys.filter(function(k) { return simple(k) === simple(name) })
        if (exact.length === 1) return exact[0]
        var aliases = keys.filter(function(k) { return table[k].indexOf(name) >= 0 })
        return aliases.length === 1 ? aliases[0] : name
    }
    function canonList(names) {
        var seen = {}, out = []
        names.forEach(function(n) {
            var p = canon(n)
            if (p && !seen[p]) { seen[p] = true; out.push(p) }
        })
        return out
    }
    function albumArtistsOf(a) { return canonList(a.artists || []) }
    function singersOf(a) {
        var names = []
        ;(a.tracks || []).forEach(function(t) { names = names.concat(t.artists || []) })
        return canonList(names)
    }
    function singersOfTrack(a, t) {
        return canonList((t.artists && t.artists.length) ? t.artists : (a.artists || []))
    }
    function initTestCase() {
        catalogue = Snapshot.data
        compare(catalogue.albums.length, 63)
    }
    function init() {
        var c = Qt.createComponent("../../qml/vinyl/VinylWorldData.qml")
        verify(c.status === Component.Ready, c.errorString())
        subject = c.createObject(this, {catalogue: catalogue})
        verify(subject)
    }
    function cleanup() { if (subject) subject.destroy() }
    function test_popularOrdered() {
        var items = subject.listItems("popular")
        verify(items.length > 10)
        for (var i = 1; i < items.length; ++i) verify(items[i-1].plays >= items[i].plays)
    }
    function test_everyShelfLensedAndEmptyAbsent() {
        subject.setProfile("seasoned")
        verify(subject.setLens("language", "Telugu"))
        subject.shelves.forEach(function(s) {
            verify(s.items.length > 0)
            s.items.forEach(function(i) { compare(i.language, "Telugu") })
        })
        verify(subject.setLens("decade", "1990s"))
        verify(subject.shelfKeys.indexOf("new") < 0)
        verify(subject.shelfKeys.indexOf("global") < 0)
    }
    function test_globalOutsideHome() {
        subject.listItems("global").forEach(function(i) {
            verify(catalogue.homeRegion.indexOf(i.country) < 0)
        })
    }
    function test_newNoPersonal() {
        compare(subject.homeRows.length, 0)
        verify(!subject.shelfKeys.some(function(k) { return k.indexOf("thread") === 0 }))
        subject.skipPicker()
        verify(subject.coldShelves.length > 0)
        subject.coldShelves.forEach(function(row) {
            row.items.forEach(function(i) { verify(!i.reason) })
        })
    }
    function test_artistOptionsLeadWithPlayedAlbumArtists() {
        var leadPlays = {}, leads = {}
        catalogue.albums.forEach(function(a) {
            if (a.playable === false) return
            albumArtistsOf(a).forEach(function(p) { leadPlays[p] = (leadPlays[p] || 0) + (a.plays || 0); leads[p] = true })
        })
        var expected = Object.keys(leadPlays).map(function(p) { return {person: p, plays: leadPlays[p]} })
            .sort(function(a,b) { return b.plays - a.plays || a.person.localeCompare(b.person) })
            .slice(0,6).map(function(e) { return e.person })
        var offered = subject.artistOptions("")
        compare(offered.slice(0,6).map(function(i) { return i.id }), expected)
        verify(offered.length > 6, "the picker must still offer everyone, not only the leads")
        var ids = offered.map(function(i) { return i.id })
        compare(ids.filter(function(v,i) { return ids.indexOf(v) === i }).length, ids.length, "no canonical person may appear twice")
        var singersOnly = {}
        catalogue.albums.forEach(function(a) {
            (a.tracks || []).forEach(function(t) {
                (t.artists || []).forEach(function(raw) { if (!leads[canon(raw)]) singersOnly[canon(raw)] = true })
            })
        })
        var probe = Object.keys(singersOnly)[0]
        verify(probe !== undefined, "catalogue must contain a person who is only a track singer")
        verify(subject.artistOptions(probe).some(function(i) { return i.id === probe }),
            "a name that is only a track singer must be found by search")
    }
    function test_reasonSeedsAreConnected() {
        subject.setProfile("seasoned")
        var byId = {}
        catalogue.albums.forEach(function(a) { byId[a.id] = a })
        var seedIds = catalogue.profiles.seasoned.history.map(function(h) { return h.albumId })
            .concat(catalogue.profiles.seasoned.saved)
        function connectedAlbum(seed, album) {
            var artists = albumArtistsOf(album).filter(function(p) { return albumArtistsOf(seed).indexOf(p) >= 0 })
            if (artists.length) return true
            var singers = singersOf(album).filter(function(p) { return singersOf(seed).indexOf(p) >= 0 })
            if (singers.length) return true
            return Math.floor(album.year/10) === Math.floor(seed.year/10) && album.language === seed.language
        }
        var rows = subject.homeRows.filter(function(r) { return r.key.indexOf("recommend-") === 0 })
        compare(rows.length, 5)
        rows.forEach(function(r) {
            verify(r.items.length > 0, r.key + " must have items")
            r.items.forEach(function(i) {
                verify(i.reason.length > 0, r.key + " items must all carry a reason")
                verify(seedIds.indexOf(i.reasonSubjectId) >= 0, "seed must come from this profile's history: " + i.reason)
                var seed = byId[i.reasonSubjectId]
                if (i.kind === "albums" || i.kind === "songs") {
                    verify(connectedAlbum(seed, byId[i.albumId]),
                        i.title + " (" + r.key + ") reason \"" + i.reason + "\" is not connected to it")
                } else if (i.kind === "artists") {
                    var credited = albumArtistsOf(seed).indexOf(i.id) >= 0 || singersOf(seed).indexOf(i.id) >= 0
                    verify(credited, "artist " + i.title + " must be credited on the album its reason names")
                } else {
                    compare(i.reasonSubjectId, String(i.id).split(":")[1], "a mix or station is its own seed's")
                }
            })
        })
        var songs = rows.find(function(r) { return r.key === "recommend-songs" })
        var distinct = []
        songs.items.forEach(function(i) { if (distinct.indexOf(i.reasonSubjectId) < 0) distinct.push(i.reasonSubjectId) })
        verify(distinct.length >= 2, "the songs row must name at least two different seeds, got " + distinct.length)
    }
    function test_reasonEvidenceAndKinds() {
        subject.setProfile("seasoned")
        var ids = catalogue.profiles.seasoned.history.map(function(h) { return h.albumId })
        catalogue.profiles.seasoned.saved.forEach(function(id) { if (ids.indexOf(id)<0) ids.push(id) })
        var rows = subject.homeRows.filter(function(r) { return r.key.indexOf("recommend-") === 0 })
        compare(rows.length, 5)
        rows.forEach(function(r) {
            r.items.forEach(function(i) {
                verify(i.reason.length > 0)
                verify(ids.indexOf(i.reasonSubjectId) >= 0, i.reason)
                compare(i.kind, r.kind)
            })
        })
        compare(subject.coldShelves.length, 0)
    }
    function test_optionsAndKinds() {
        subject.setLens("language", "Telugu")
        subject.setLens("decade", "1990s")
        verify(subject.filterOptions("country").some(function(o) { return o.value === "United States" && !o.enabled }))
        verify(!subject.setLens("country", "United States"))
        subject.setKind("artists")
        subject.shelves.forEach(function(s) {
            var ids = []
            s.items.forEach(function(i) { compare(i.kind, "artists"); verify(ids.indexOf(i.id)<0); ids.push(i.id) })
        })
    }
    function test_profilePickerRetention() {
        subject.skipPicker()
        subject.setProfile("seasoned")
        subject.setProfile("new")
        verify(subject.pickerAnswered)
        subject.resetPicker()
        verify(!subject.pickerAnswered)
        var artists = subject.artistOptions("").slice(0,3)
        subject.answerPicker(artists.map(function(i) {return i.id}))
        verify(subject.pickerAnswered)
        verify(subject.homeRows.length > 0)
        compare(subject.coldShelves.length, 0)
        subject.homeRows.filter(function(r) {return r.key.indexOf("recommend-") === 0}).forEach(function(r) {
            r.items.forEach(function(i) { verify(subject.picks.indexOf(i.reasonSubjectId)>=0) })
        })
    }
    function test_threadIdentitySurvivesLens() {
        subject.setProfile("seasoned")
        var thread=subject.shelves.find(function(s){return s.key.indexOf("thread")===0})
        verify(thread!==undefined)
        var title=thread.title,key=thread.key
        subject.setLens("language","English")
        compare(subject.row(key,false).title,title)
    }
    function test_offlineProfileIsolation() {
        subject.setProfile("seasoned")
        verify(subject.shelfKeys.some(function(k) {return k.indexOf("thread")===0}))
        subject.setCatalogueReachable(false)
        subject.setProfile("new")
        verify(!subject.shelfKeys.some(function(k) {return k.indexOf("thread")===0}))
        subject.setProfile("seasoned")
        verify(subject.shelfKeys.some(function(k) {return k.indexOf("thread")===0}))
    }
    function test_recentArtistPicksEarnRecentRow() {
        subject.answerPicker(["Thaman S","Anirudh Ravichander","Shreya Ghoshal"])
        verify(subject.homeRows.some(function(r) {return r.key==="new-artists"}))
    }
    function test_offlineRetainsAndUnplayableAbsent() {
        subject.setProfile("seasoned")
        var keys = JSON.stringify(subject.shelfKeys), rows = JSON.stringify(subject.homeRows)
        subject.setCatalogueReachable(false)
        verify(!subject.setLens("language", "Telugu"))
        compare(JSON.stringify(subject.shelfKeys), keys)
        compare(JSON.stringify(subject.homeRows), rows)
        var changed = JSON.parse(JSON.stringify(catalogue))
        var banned = changed.albums[0].id
        changed.albums[0].playable = false
        subject.setCatalogueReachable(true)
        subject.catalogue = changed
        subject.shelves.concat(subject.homeRows).forEach(function(r) {
            r.items.forEach(function(i) { verify(i.albumId !== banned) })
        })
    }
}
