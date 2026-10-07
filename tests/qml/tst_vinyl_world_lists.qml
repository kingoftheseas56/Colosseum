import QtQuick
import QtTest
import "../fixtures/vinyl-world/Catalogue.js" as Snapshot

TestCase {
    name: "VinylWorldLists"
    property var subject
    property var catalogue
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
