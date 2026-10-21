import QtQuick

Item {
    id: root

    property string symbol: "play"
    property color color: "white"
    implicitWidth: 18
    implicitHeight: 18

    readonly property string iconMarkup: {
        switch (root.symbol) {
        case "play":
            return "<path d=\"M8 5.5v13l10-6.5z\" fill=\"ICON_COLOR\" stroke=\"none\"/>";
        case "playNext":
            return "<path d=\"M5.5 5.5v13l10-6.5z\" fill=\"ICON_COLOR\" stroke=\"none\"/><path d=\"M19 5v14\"/>";
        case "pause":
            return "<path d=\"M7 5h3.5v14H7zM13.5 5H17v14h-3.5z\" fill=\"ICON_COLOR\" stroke=\"none\"/>";
        case "shuffle":
            return "<path d=\"M4 7h2.5c4.5 0 6.5 10 11 10H20M16 14l4 3-4 3M4 17h2.5c1.4 0 2.5-1 3.5-2.5M14 8.5c1.1-1 2.2-1.5 3.5-1.5H20M16 4l4 3-4 3\"/>";
        case "save":
            return "<path d=\"M20.5 8.7c0 5-8.5 10-8.5 10s-8.5-5-8.5-10A4.5 4.5 0 0 1 12 6.2a4.5 4.5 0 0 1 8.5 2.5Z\"/>";
        case "saved":
            return "<path d=\"M20.5 8.7c0 5-8.5 10-8.5 10s-8.5-5-8.5-10A4.5 4.5 0 0 1 12 6.2a4.5 4.5 0 0 1 8.5 2.5Z\" fill=\"ICON_COLOR\"/>";
        case "queue":
            return "<path d=\"M4 6h10M4 11h10M4 16h6M18 13v8M14 17h8\"/>";
        case "musicFiles":
            return "<path d=\"M5 4.5h8l3 3v3M13 4.5v3h3M7 19.5h10M8 16V9l6-1.5v7\"/><circle cx=\"6.5\" cy=\"16\" r=\"1.5\"/><circle cx=\"12.5\" cy=\"14.5\" r=\"1.5\"/>";
        case "musicFolder":
            return "<path d=\"M3.5 7.5h7l2 2h8v9h-17zM3.5 7.5v-2h6l2 2\"/><path d=\"M10 16V12l4-1v4\"/><circle cx=\"8.5\" cy=\"16\" r=\"1.5\"/><circle cx=\"12.5\" cy=\"15\" r=\"1.5\"/>";
        case "nowPlaying":
            return "<circle cx=\"12\" cy=\"12\" r=\"9\"/><circle cx=\"12\" cy=\"12\" r=\"2\"/><path d=\"M12 12l5-5\"/>";
        case "search":
            return "<circle cx=\"10.8\" cy=\"10.8\" r=\"6.3\"/><path d=\"m15.5 15.5 4.2 4.2\"/>";
        case "lyrics":
            return "<path d=\"M5 5.5h14v13H5zM8 9h8M8 12h8M8 15h5\"/><path d=\"M7 3.5h10\"/>";
        case "notes":
            return "<path d=\"M6 3.5h8l4 4v13H6zM14 3.5v4h4M9 12h6M9 15h3\"/><path d=\"M15 17v-4l3-.7v4\"/><circle cx=\"13.8\" cy=\"17.3\" r=\"1.2\"/>";
        case "credits":
            return "<circle cx=\"9\" cy=\"8\" r=\"3\"/><path d=\"M3.8 19c.4-3.1 2.4-5 5.2-5s4.8 1.9 5.2 5M16 5.5a3 3 0 0 1 0 5.8M16.5 14c2.1.5 3.3 2.1 3.7 4.5\"/>";
        case "cancel":
            return "<path d=\"m6 6 12 12M18 6 6 18\"/>";
        case "retry":
            return "<path d=\"M20 7v5h-5M4.8 9A7.5 7.5 0 0 1 18 6.5L20 12M4 17v-5h5M19.2 15A7.5 7.5 0 0 1 6 17.5L4 12\"/>";
        case "sort":
            return "<path d=\"M4 6h10M4 12h7M4 18h4M18 4v16M15 7l3-3 3 3M15 17l3 3 3-3\"/>";
        case "check":
            return "<path d=\"m5 12 4 4L19 6\"/>";
        case "plus":
            return "<path d=\"M12 5v14M5 12h14\"/>";
        case "edit":
            return "<path d=\"M12 20h9\"/><path d=\"M16.5 3.5a2.1 2.1 0 0 1 3 3L8 18l-4 1 1-4Z\"/>";
        case "trash":
            return "<path d=\"M3 6h18M8 6V4h8v2M6 6l1 15h10l1-15M10 10v7M14 10v7\"/>";
        case "arrowLeft":
            return "<path d=\"M19 12H5M12 19l-7-7 7-7\"/>";
        case "arrowUp":
            return "<path d=\"M12 19V5M5 12l7-7 7 7\"/>";
        case "arrowDown":
            return "<path d=\"M12 5v14M19 12l-7 7-7-7\"/>";
        default:
            return "<path d=\"M5 4.5h8l3 3v3M13 4.5v3h3M7 19.5h10M8 16V9l6-1.5v7\"/><circle cx=\"6.5\" cy=\"16\" r=\"1.5\"/><circle cx=\"12.5\" cy=\"14.5\" r=\"1.5\"/>";
        }
    }

    readonly property url svgSource: {
        var rgb = "rgb(" + Math.round(root.color.r * 255) + ","
            + Math.round(root.color.g * 255) + ","
            + Math.round(root.color.b * 255) + ")";
        var body = root.iconMarkup.replace(/ICON_COLOR/g, rgb);
        var svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\"><g fill=\"none\" stroke=\""
            + rgb + "\" opacity=\"" + root.color.a
            + "\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" fill-rule=\"evenodd\">"
            + body + "</g></svg>";
        return "data:image/svg+xml;utf8," + encodeURIComponent(svg);
    }

    Image {
        anchors.fill: parent
        source: root.svgSource
        sourceSize.width: 24
        sourceSize.height: 24
        fillMode: Image.PreserveAspectFit
        smooth: true
    }
}
