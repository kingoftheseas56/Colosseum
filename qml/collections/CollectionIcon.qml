import QtQuick
import ".."

Item {
    id: root
    property string name: "collections"
    property color strokeColor: "#c9c8d0"
    implicitWidth: 20
    implicitHeight: 20
    Accessible.ignored: true
    Image {
        anchors.fill: parent
        sourceSize.width: Math.max(1, Math.ceil(root.width))
        sourceSize.height: Math.max(1, Math.ceil(root.height))
        fillMode: Image.PreserveAspectFit
        source: "data:image/svg+xml;utf8," + encodeURIComponent(
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="' + root.strokeColor
            + '" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">' + (root.paths[root.name] || root.paths.collections) + '</svg>')
    }
    readonly property var paths: ({
        collections: '<rect x="6" y="7" width="15" height="14" rx="2"/><path d="M3 17V5a2 2 0 0 1 2-2h12M10 11h7m-7 5h5"/>',
        folder: '<path d="M3 7a2 2 0 0 1 2-2h5l2 2h7a2 2 0 0 1 2 2v10H3Z"/>',
        colosseum: '<path d="M3 20h18M4 20V8l8-4 8 4v12M4 10h16M7 8V6m5 4V4m5 4V6M7 20v-5a2 2 0 0 1 4 0v5m2 0v-5a2 2 0 0 1 4 0v5"/>',
        plus: '<path d="M12 5v14M5 12h14"/>',
        close: '<path d="m6 6 12 12M6 18 18 6"/>',
        check: '<path d="m5 12 4 4L19 6"/>',
        search: '<circle cx="10.5" cy="10.5" r="6.5"/><path d="m16 16 4.5 4.5"/>',
        back: '<path d="M19 12H5m5-5-5 5 5 5"/>',
        arrow: '<path d="M5 12h14m-5-5 5 5-5 5"/>',
        film: '<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M7 3v18m10-18v18M3 8h4m-4 8h4M17 8h4m-4 8h4"/>',
        series: '<rect x="3" y="5" width="18" height="14" rx="2"/><path d="M8 22h8M9 2l3 3 3-3"/>',
        album: '<rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="12" cy="12" r="5"/><circle cx="12" cy="12" r="1"/>',
        song: '<path d="M9 18V5l11-2v13M9 9l11-2"/><ellipse cx="6" cy="18" rx="3" ry="2"/><ellipse cx="17" cy="16" rx="3" ry="2"/>',
        artist: '<circle cx="12" cy="7" r="4"/><path d="M4 21v-3a8 8 0 0 1 16 0v3"/>',
        manga: '<path d="M3 5c3-1 6-1 9 1 3-2 6-2 9-1v15c-3-1-6-1-9 1-3-2-6-2-9-1Z"/><path d="M12 6v15M6 9h3m-3 4h3m6-4h3m-3 4h3"/>',
        book: '<path d="M6 4h14v17H6a3 3 0 0 1-3-3V7a3 3 0 0 1 3-3Z"/><path d="M6 4v12h14M6 18h13"/>',
        comic: '<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M3 10h18M11 10v11M3 15h8M16 3v7"/>',
        grid: '<rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/>',
        list: '<path d="M8 5h13M8 12h13M8 19h13M3 5h.01M3 12h.01M3 19h.01"/>',
        edit: '<path d="m15 5 4 4M4 20l4-1L20 7a2.1 2.1 0 0 0-3-3L5 16Z"/>',
        lock: '<rect x="5" y="10" width="14" height="11" rx="2"/><path d="M8 10V6a4 4 0 0 1 8 0v4m-4 5v2"/>',
        up: '<path d="m6 15 6-6 6 6"/>',
        down: '<path d="m6 9 6 6 6-6"/>',
        more: '<circle cx="5" cy="12" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="19" cy="12" r="1"/>',
        trash: '<path d="M3 6h18M9 6V3h6v3M5 6l1 15h12l1-15M9 10v7m6-7v7"/>',
        note: '<path d="M5 3h14v18H5ZM8 7h8m-8 4h8m-8 4h5"/>',
        reset: '<path d="M3 10a9 9 0 1 1 1 8M3 4v6h6"/>'
    })
}
