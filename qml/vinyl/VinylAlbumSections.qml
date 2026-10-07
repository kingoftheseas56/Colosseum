import QtQuick
import QtQuick.Controls
import ".."

Column {
    id: root
    required property var pageData
    property int editionThreshold: 4
    property bool creditsExpanded: false
    property bool editionsExpanded: false
    readonly property var sectionRows: [
        {
            key: "about",
            iconSource: sectionIcon("about", '<path d="M5 4h14v16H5zM8 8h8M8 12h8M8 16h5"/>'),
            title: "About",
            target: about
        },
        {
            key: "credits",
            iconSource: sectionIcon("credits", '<circle cx="9" cy="8" r="3"/><path d="M3 20v-2a6 6 0 0 1 12 0v2M16 5a3 3 0 0 1 0 6M18 14a5 5 0 0 1 3 4v2"/>'),
            title: "Credits",
            target: credits
        },
        {
            key: "videos",
            iconSource: sectionIcon("videos", '<rect x="3" y="5" width="18" height="14" rx="3"/><path d="m10 9 5 3-5 3z"/>'),
            title: "Videos",
            target: videos
        },
        {
            key: "editions",
            iconSource: sectionIcon("editions", '<rect x="3" y="7" width="14" height="14" rx="2"/><path d="M7 3h12a2 2 0 0 1 2 2v12"/><circle cx="10" cy="14" r="3"/>'),
            title: "Editions",
            target: editions
        },
        {
            key: "more",
            iconSource: sectionIcon("more", '<path d="M9 18V5l11-2v13M9 8l11-2"/><ellipse cx="6" cy="18" rx="3" ry="2"/><ellipse cx="17" cy="16" rx="3" ry="2"/>'),
            title: "More by this artist",
            target: more
        },
        {
            key: "information",
            iconSource: sectionIcon("information", '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7v.1"/>'),
            title: "Information",
            target: info
        }
    ]
    function sectionIcon(key, paths) {
        var ink = theme.inkDim;
        return "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="' + ink + '" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">' + paths + '</svg>');
    }
    function countryIcon(country) {
        var content = '';
        if (country === "United States") {
            content = '<rect width="38" height="24" fill="#fff"/>';
            for (var i = 0; i < 13; i += 2)
                content += '<rect y="' + (i * 24 / 13) + '" width="38" height="' + (24 / 13) + '" fill="#b22234"/>';
            content += '<rect width="16" height="13" fill="#3c3b6e"/>';
            for (var row = 0; row < 9; row++)
                for (var col = 0; col < (row % 2 ? 5 : 6); col++)
                    content += '<circle cx="' + (1.5 + col * 2.55 + (row % 2 ? 1.2 : 0)) + '" cy="' + (1 + row * 1.35) + '" r=".45" fill="#fff"/>';
        } else
            content = '<g fill="none" stroke="' + theme.inkDim + '" stroke-width="1.2"><circle cx="19" cy="12" r="10"/><ellipse cx="19" cy="12" rx="4" ry="10"/><path d="M9 12h20M11 6h16M11 18h16"/></g>';
        return 'data:image/svg+xml,' + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 38 24">' + content + '</svg>');
    }
    signal videoRequested(int track)
    spacing: 64
    Theme {
        id: theme
    }
    component Heading: Text {
        color: theme.ink
        font.family: theme.display
        font.pixelSize: 32
    }
    component Link: KeyboardAction {
        id: link
        property bool editorial: false
        property string label
        implicitWidth: text.implicitWidth
        implicitHeight: 30
        accessibleName: label
        Text {
            id: text
            text: link.label
            color: link.interactionActive ? theme.gold : theme.inkDim
            font.family: link.editorial ? theme.display : theme.ui
            font.pixelSize: link.editorial ? 22 : 13
        }
    }
    Column {
        id: about
        objectName: "vinylSection_about"
        width: parent.width
        spacing: 20
        Heading {
            text: "About"
        }
        Text {
            width: Math.min(parent.width, 880)
            text: root.pageData.shownEdition.about
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 16
            wrapMode: Text.WordWrap
            lineHeight: 1.55
        }
    }
    Column {
        id: credits
        objectName: "vinylSection_credits"
        width: parent.width
        spacing: 24
        Heading {
            text: "Credits"
        }
        Grid {
            id: creditsGrid
            objectName: "vinylCreditsGrid"
            width: parent.width
            columns: 3
            columnSpacing: 36
            rowSpacing: 25
            readonly property int groupCount: root.creditsExpanded ? root.pageData.credits.length : Math.min(3, root.pageData.credits.length)
            Repeater {
                model: creditsGrid.groupCount
                delegate: Column {
                    required property int index
                    width: (creditsGrid.width - 72) / 3
                    spacing: 8
                    Text {
                        text: root.pageData.credits[parent.index].role.toUpperCase()
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 11
                        font.letterSpacing: 1.5
                    }
                    Repeater {
                        model: root.pageData.credits[index].names
                        delegate: Link {
                            required property string modelData
                            editorial: true
                            label: modelData
                            onTriggered: root.pageData.artistRequested(modelData)
                        }
                    }
                }
            }
        }
        Link {
            objectName: "vinylAllCredits"
            label: root.creditsExpanded ? "Fewer credits" : "All credits"
            onTriggered: root.creditsExpanded = !root.creditsExpanded
        }
    }
    Column {
        id: videos
        objectName: "vinylSection_videos"
        width: parent.width
        spacing: 24
        Heading {
            text: "Videos"
        }
        Row {
            spacing: 18
            width: parent.width
            Repeater {
                model: root.pageData.videos
                delegate: KeyboardAction {
                    required property var modelData
                    required property int index
                    objectName: "vinylVideo_" + index
                    width: (videos.width - 36) / 3
                    height: width * 9 / 16 + 36
                    accessibleName: modelData.title
                    onTriggered: root.videoRequested(modelData.track)
                    Image {
                        width: parent.width
                        height: width * 9 / 16
                        source: root.pageData.artwork(parent.modelData.cover)
                        fillMode: Image.PreserveAspectCrop
                    }
                    Text {
                        y: parent.width * 9 / 16 + 10
                        text: parent.modelData.title
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 14
                    }
                }
            }
        }
    }
    Column {
        id: editions
        objectName: "vinylSection_editions"
        width: parent.width
        spacing: 20
        readonly property var sortedEditions: root.pageData.editions.slice().sort(function (a, b) {
            return a.year - b.year;
        })
        readonly property var editionYears: sortedEditions.map(function (e) {
            return e.year;
        })
        Heading {
            text: "Editions"
        }
        Repeater {
            model: root.editionsExpanded ? editions.sortedEditions : editions.sortedEditions.slice(0, root.editionThreshold)
            delegate: KeyboardAction {
                required property var modelData
                objectName: "vinylEdition_" + modelData.id
                width: editions.width
                height: 75
                accessibleName: modelData.subtitle || modelData.name
                onTriggered: root.pageData.selectEdition(modelData.id)
                Image {
                    width: 58
                    height: 58
                    source: root.pageData.artwork(parent.modelData.cover)
                    fillMode: Image.PreserveAspectCrop
                }
                Column {
                    x: 78
                    width: parent.width - 120
                    spacing: 8
                    Text {
                        text: parent.parent.modelData.subtitle || parent.parent.modelData.name
                        color: root.pageData.shownEditionId === parent.parent.modelData.id ? theme.gold : theme.ink
                        font.family: theme.display
                        font.pixelSize: 23
                    }
                    Text {
                        text: [parent.parent.modelData.year, parent.parent.modelData.label, parent.parent.modelData.country, parent.parent.modelData.format, root.pageData.totalTracks(parent.parent.modelData) + " tracks"].filter(Boolean).join(" · ")
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 12
                    }
                }
                Text {
                    anchors.right: parent.right
                    text: root.pageData.shownEditionId === parent.modelData.id ? "✓" : ""
                    color: theme.gold
                    font.family: theme.ui
                    font.pixelSize: 18
                }
            }
        }
        Link {
            objectName: "vinylAllEditions"
            visible: root.pageData.editions.length > root.editionThreshold
            label: root.editionsExpanded ? "Fewer editions" : "All editions"
            onTriggered: root.editionsExpanded = !root.editionsExpanded
        }
    }
    Column {
        id: more
        objectName: "vinylSection_more"
        width: parent.width
        spacing: 24
        Heading {
            text: "More by this artist"
        }
        Row {
            width: parent.width
            spacing: 20
            Repeater {
                model: root.pageData.moreAlbums
                delegate: Column {
                    required property var modelData
                    width: (more.width - 80) / 5
                    spacing: 10
                    Image {
                        width: parent.width
                        height: width
                        source: root.pageData.artwork(parent.modelData.cover)
                        fillMode: Image.PreserveAspectCrop
                    }
                    Text {
                        width: parent.width
                        text: parent.modelData.title
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 14
                        elide: Text.ElideRight
                    }
                    Text {
                        text: parent.modelData.year
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
    Column {
        id: info
        objectName: "vinylSection_information"
        width: parent.width
        spacing: 26
        Heading {
            text: "Information"
        }
        Grid {
            id: infoGrid
            width: parent.width
            columns: root.width > 1100 ? 3 : 2
            columnSpacing: 60
            rowSpacing: 32
            Repeater {
                model: root.pageData.information
                delegate: Column {
                    required property var modelData
                    objectName: "vinylInfo_" + modelData[0]
                    width: (info.width - (infoGrid.columns - 1) * 60) / infoGrid.columns
                    spacing: 12
                    Text {
                        text: parent.modelData[1].toUpperCase()
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 11
                        font.letterSpacing: 1.3
                    }
                    VinylBarcode {
                        visible: parent.modelData[0] === "barcode"
                        digits: visible ? String(parent.modelData[2]) : ""
                    }
                    Row {
                        visible: parent.modelData[0] !== "barcode"
                        spacing: 12
                        Image {
                            visible: parent.parent.modelData[0] === "country"
                            width: 32
                            height: 21
                            source: root.countryIcon(parent.parent.modelData[2])
                        }
                        Text {
                            text: parent.parent.modelData[2]
                            color: theme.inkDim
                            font.family: parent.parent.modelData[0] === "catalogue" ? theme.display : theme.ui
                            font.pixelSize: parent.parent.modelData[0] === "catalogue" ? 30 : 16
                        }
                    }
                }
            }
        }
    }
}
