import QtQml

QtObject {
    property int revision: 0
    property var roots: []
    property int itemCount: 0
    property bool scanning: false
    property bool cardVisible: false
    property var identityCeremonies: []
    property var candidate: []
    property string candidateRoot: ""
    property int scanDone: 0
    property int scanTotal: 0
    property string scanningRoot: ""
    signal browseArtResolved(string key)
    signal scanProgressChanged()
    function setRoots(value) { roots = value; revision++ }
    function rootsDetail() { return roots }
    function rootCount() { return roots.length }
    function admissionById() { return ({}) }
    function downloadsRootPath() { return "" }
    function scanIgnore() { return [] }
    function hiddenSeries() { return [] }
    function recentArrivals(limit) { return [] }
    function browseAt(path, sort, filters) { return [] }
    function browseEmptyCause(path, filters) { return "away" }
    function browseEmptyAwayCount(path) { return 0 }
    function seasonFactsForShow(path) { return ({ seasons: [] }) }
    function offerUnconfirmedRoots() {}
    function rescanDegradedRoots() {}
}
