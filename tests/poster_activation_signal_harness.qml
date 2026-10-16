// Offscreen MouseArea signal-handler dispatch (not a synthetic physical pointer claim).
import QtQuick
import "../qml" as UI

Item {
    id: root
    width: 650; height: 400
    property var sample: ({title:"Original object",year:"2026",rating:"8.9",cover:""})
    property int calls: 0
    property var last: null
    property var fails: []
    function check(v, name) { if (!v) fails.push(name) }
    function area(n) {
        if (!n) return null
        if (n.cursorShape !== undefined && n.containsMouse !== undefined &&
            n.pressed !== undefined && n.hoverEnabled !== undefined) return n
        var c = n.children || []
        for (var i=0;i<c.length;i++) { var f=area(c[i]); if(f) return f }
        return null
    }
    UI.CataloguePosterCard {
        id: classic
        x: 20; y: 20; width: 140; height: 250; item: root.sample
        onActivated: function(value) { root.calls++; root.last=value }
    }
    UI.CataloguePosterCard {
        id: gallery
        x: 220; y: 20; width: 148; height: 260
        item: root.sample; visualProfile: "gallery"
        onActivated: function(value) { root.calls++; root.last=value }
    }
    Timer {
        interval: 90; running: true; repeat: false
        onTriggered: {
            var ca = root.area(classic), ga = root.area(gallery)
            root.check(!!ca && !!ga, "both actual MouseArea handlers are present")
            if (ca && ga) {
                root.check(ca.enabled && ga.enabled, "real cards' MouseAreas enabled")
                ca.clicked(null)
                root.check(root.calls===1 && root.last===root.sample,
                           "classic onClicked forwards original item identity")
                ga.clicked(null)
                root.check(root.calls===2 && root.last===root.sample,
                           "gallery onClicked forwards original item identity")
                classic.skeleton=true
                root.check(!ca.enabled, "skeleton disables existing MouseArea")
                classic.skeleton=false
                root.check(ca.enabled, "real return re-enables existing MouseArea")
            }
            console.log(root.fails.length ?
                "POSTER_SIGNAL_RED " + root.fails.join("; ") :
                "POSTER_SIGNAL_GREEN actual-MouseArea-click-handler-identity-and-skeleton-gate")
            Qt.exit(root.fails.length ? 2 : 0)
        }
    }
}