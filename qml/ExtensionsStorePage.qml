// ExtensionsStorePage — Store landing page.
// Colosseum shell + Stremio Addons catalogue structure + Harbor-inspired essential cards.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Item {
    id: root
    objectName: "extensionsStorePage"

    signal sectionRequested(string section)
    signal essentialRequested(string slug)
    signal categoryRequested(string category)
    signal searchRequested(string query)

    Theme { id: theme }
    FontLoader { id: frauncesFont; source: "../assets/fonts/Fraunces-Regular.ttf" }

    readonly property color gold: "#efc15a"
    readonly property color ivory: "#f7f7f5"
    readonly property string uiFamily: theme.ui
    readonly property string displayFamily: frauncesFont.status === FontLoader.Ready ? frauncesFont.name : theme.display

    Rectangle {
        anchors.fill: parent
        color: "#050609"
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.00; color: "#101118" }
            GradientStop { position: 0.36; color: "#08090d" }
            GradientStop { position: 1.00; color: "#040507" }
        }
    }

    Rectangle {
        width: root.width * 0.52
        height: width
        x: -width * 0.36
        y: root.height * 0.13
        radius: width / 2
        color: "#5a4774"
        opacity: 0.055
    }

    Rectangle {
        width: root.width * 0.44
        height: width
        x: root.width * 0.72
        y: root.height * 0.52
        radius: width / 2
        color: "#8a663c"
        opacity: 0.045
    }

    component TintIcon: Item {
        id: tintIcon
        required property url source
        property color ink: "#ffffff"

        Image {
            id: tintIconSource
            anchors.fill: parent
            source: tintIcon.source
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            visible: false
        }

        MultiEffect {
            anchors.fill: tintIconSource
            source: tintIconSource
            colorization: 1
            colorizationColor: tintIcon.ink
        }
    }

    component EssentialCard: Rectangle {
        id: essentialCard
        required property string title
        required property string slug
        required property string description
        required property string stars
        required property string actionText
        required property url iconSource

        height: 154
        radius: 22
        color: "#101116"
        border.width: 1
        border.color: essentialMouse.containsMouse || activeFocus
                      ? Qt.rgba(1,1,1,0.22)
                      : Qt.rgba(1,1,1,0.105)
        clip: true
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: title

        Image {
            anchors.fill: parent
            source: essentialCard.iconSource
            fillMode: Image.PreserveAspectCrop
            opacity: 0.10
            scale: 1.65
            smooth: true
            mipmap: true
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.00; color: "#101116" }
                GradientStop { position: 0.43; color: "#101116" }
                GradientStop { position: 0.72; color: Qt.rgba(16/255,17/255,22/255,0.80) }
                GradientStop { position: 1.00; color: Qt.rgba(16/255,17/255,22/255,0.38) }
            }
        }

        Rectangle {
            id: essentialLogoPlate
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            width: 64
            height: 64
            radius: 14
            color: Qt.rgba(1,1,1,0.055)
            border.width: 1
            border.color: Qt.rgba(1,1,1,0.10)

            Image {
                anchors.fill: parent
                anchors.margins: 4
                source: essentialCard.iconSource
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
                cache: true
            }
        }

        Column {
            anchors.left: essentialLogoPlate.right
            anchors.leftMargin: 18
            anchors.right: actionPill.left
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 7

            Row {
                spacing: 8

                Text {
                    text: essentialCard.title
                    color: root.ivory
                    font.family: root.uiFamily
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: starText.implicitWidth + 13
                    height: 22
                    radius: 11
                    color: Qt.rgba(240/255,193/255,90/255,0.11)
                    border.width: 1
                    border.color: Qt.rgba(240/255,193/255,90/255,0.28)

                    Text {
                        id: starText
                        anchors.centerIn: parent
                        text: "★ " + essentialCard.stars
                        color: root.gold
                        font.family: root.uiFamily
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                }
            }

            Text {
                width: parent.width
                text: essentialCard.description
                color: Qt.rgba(1,1,1,0.55)
                font.family: root.uiFamily
                font.pixelSize: 12
                lineHeight: 1.3
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }

        Rectangle {
            id: actionPill
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            width: actionText.implicitWidth + 30
            height: 36
            radius: 18
            color: root.ivory

            Text {
                id: actionText
                anchors.centerIn: parent
                text: essentialCard.actionText
                color: "#111217"
                font.family: root.uiFamily
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
        }

        Rectangle {
            visible: essentialCard.activeFocus
            anchors.fill: parent
            anchors.margins: -2
            radius: parent.radius + 2
            color: "transparent"
            border.width: 2
            border.color: root.gold
        }

        scale: essentialMouse.containsMouse || activeFocus ? 1.004 : 1
        Behavior on scale {
            NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
        }

        MouseArea {
            id: essentialMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                essentialCard.forceActiveFocus(Qt.MouseFocusReason)
                root.essentialRequested(essentialCard.slug)
            }
        }

        Keys.onReturnPressed: root.essentialRequested(slug)
        Keys.onSpacePressed: root.essentialRequested(slug)
    }

    component CategoryBanner: Rectangle {
        id: banner
        required property string title
        required property string category
        required property string subtitle
        required property int extensionCount
        required property color accent
        required property color accent2
        required property url artIcon

        width: categoryColumn.width
        height: 210
        radius: 28
        color: "#0f1116"
        border.width: 1
        border.color: bannerMouse.containsMouse || activeFocus
                      ? Qt.rgba(1,1,1,0.22)
                      : Qt.rgba(1,1,1,0.10)
        clip: true
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: title + ", " + extensionCount + " extensions"

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.00; color: Qt.darker(banner.accent, 2.45) }
                GradientStop { position: 0.52; color: Qt.darker(banner.accent2, 2.85) }
                GradientStop { position: 1.00; color: "#0a0b0f" }
            }
        }

        Rectangle {
            width: parent.width * 0.43
            height: width
            radius: width / 2
            x: parent.width * 0.68
            y: -parent.height * 1.22
            color: banner.accent
            opacity: 0.22
        }

        Rectangle {
            width: parent.width * 0.31
            height: width
            radius: width / 2
            x: parent.width * 0.78
            y: parent.height * 0.16
            color: banner.accent2
            opacity: 0.14
        }

        Item {
            anchors.right: parent.right
            anchors.rightMargin: 34
            anchors.verticalCenter: parent.verticalCenter
            width: 360
            height: 170

            Rectangle {
                x: 28
                y: 27
                width: 92
                height: 126
                radius: 12
                rotation: -8
                color: Qt.rgba(1,1,1,0.035)
                border.width: 1
                border.color: Qt.rgba(1,1,1,0.09)
            }

            Rectangle {
                x: 112
                y: 18
                width: 98
                height: 134
                radius: 12
                rotation: 4
                color: Qt.rgba(1,1,1,0.050)
                border.width: 1
                border.color: Qt.rgba(1,1,1,0.11)
            }

            Rectangle {
                x: 202
                y: 31
                width: 92
                height: 122
                radius: 12
                rotation: 9
                color: Qt.rgba(1,1,1,0.028)
                border.width: 1
                border.color: Qt.rgba(1,1,1,0.08)
            }

            TintIcon {
                anchors.right: parent.right
                anchors.rightMargin: 22
                anchors.verticalCenter: parent.verticalCenter
                width: 118
                height: 118
                source: banner.artIcon
                ink: Qt.rgba(1,1,1,0.72)
                opacity: 0.48
            }
        }

        Column {
            anchors.left: parent.left
            anchors.leftMargin: 34
            anchors.right: parent.right
            anchors.rightMargin: 410
            anchors.verticalCenter: parent.verticalCenter
            spacing: 9

            Text {
                text: "CATEGORY · " + banner.extensionCount + " EXTENSIONS"
                color: root.gold
                font.family: root.uiFamily
                font.pixelSize: 9
                font.weight: Font.DemiBold
                font.letterSpacing: 1.5
            }

            Text {
                text: banner.title
                color: root.ivory
                font.family: root.displayFamily
                font.pixelSize: 38
                font.weight: Font.Medium
            }

            Text {
                width: parent.width
                text: banner.subtitle
                color: Qt.rgba(1,1,1,0.60)
                font.family: root.uiFamily
                font.pixelSize: 13
                lineHeight: 1.25
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 20
            width: 34
            height: 34
            radius: 17
            color: Qt.rgba(1,1,1,0.08)
            border.width: 1
            border.color: Qt.rgba(1,1,1,0.12)

            Text {
                anchors.centerIn: parent
                text: "→"
                color: root.ivory
                font.family: root.uiFamily
                font.pixelSize: 18
            }
        }

        scale: bannerMouse.containsMouse || activeFocus ? 1.004 : 1
        Behavior on scale {
            NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
        }

        MouseArea {
            id: bannerMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                banner.forceActiveFocus(Qt.MouseFocusReason)
                root.categoryRequested(banner.category)
            }
        }

        Keys.onReturnPressed: root.categoryRequested(category)
        Keys.onSpacePressed: root.categoryRequested(category)
    }

    Flickable {
        id: pageFlick
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: pageColumn.implicitHeight + 90

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: pageColumn
            width: Math.min(pageFlick.width - 104, 1540)
            anchors.horizontalCenter: parent.horizontalCenter
            topPadding: 126
            bottomPadding: 80
            spacing: 0

            Item {
                id: hero
                width: parent.width
                height: 560

                Item {
                    id: heroCopy
                    anchors.left: parent.left
                    anchors.top: parent.top
                    width: parent.width * 0.56
                    height: parent.height

                    Text {
                        id: eyebrow
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.topMargin: 10
                        text: "COMMUNITY EXTENSIONS · LIVE CATALOGUE"
                        color: root.gold
                        font.family: root.uiFamily
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 2.3
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: eyebrow.bottom
                        anchors.topMargin: 28
                        text: "Find the exact piece\nyour library is missing."
                        color: root.ivory
                        font.family: root.displayFamily
                        font.pixelSize: Math.max(58, Math.min(92, hero.width * 0.060))
                        font.weight: Font.Normal
                        lineHeight: 0.86
                        font.letterSpacing: -2.5
                    }

                    Text {
                        id: heroDescription
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 302
                        text: "A quieter way through the Stremio extension ecosystem. Browse by what an extension actually does, then narrow by category when you want to go deeper."
                        color: Qt.rgba(1,1,1,0.70)
                        font.family: root.uiFamily
                        font.pixelSize: 16
                        lineHeight: 1.45
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        id: heroButtons
                        anchors.left: parent.left
                        anchors.top: heroDescription.bottom
                        anchors.topMargin: 30
                        spacing: 10

                        Rectangle {
                            width: browseText.implicitWidth + 34
                            height: 46
                            radius: 14
                            color: root.ivory
                            Text {
                                id: browseText
                                anchors.centerIn: parent
                                text: "Browse all extensions"
                                color: "#111217"
                                font.family: root.uiFamily
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }
                        }

                        Rectangle {
                            width: categoryText.implicitWidth + 34
                            height: 46
                            radius: 14
                            color: Qt.rgba(1,1,1,0.035)
                            border.width: 1
                            border.color: Qt.rgba(1,1,1,0.13)
                            Text {
                                id: categoryText
                                anchors.centerIn: parent
                                text: "Explore categories"
                                color: root.ivory
                                font.family: root.uiFamily
                                font.pixelSize: 14
                            }
                        }
                    }

                    Row {
                        anchors.left: parent.left
                        anchors.top: heroButtons.bottom
                        anchors.topMargin: 34
                        spacing: 30

                        Text {
                            text: "<b>593</b>  extensions"
                            textFormat: Text.RichText
                            color: Qt.rgba(1,1,1,0.52)
                            font.family: root.uiFamily
                            font.pixelSize: 12
                        }
                        Text {
                            text: "<b>13</b>  categories"
                            textFormat: Text.RichText
                            color: Qt.rgba(1,1,1,0.52)
                            font.family: root.uiFamily
                            font.pixelSize: 12
                        }
                        Text {
                            text: "<b>40</b>  rising now"
                            textFormat: Text.RichText
                            color: Qt.rgba(1,1,1,0.52)
                            font.family: root.uiFamily
                            font.pixelSize: 12
                        }
                    }
                }

                Rectangle {
                    id: featuredCard
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 4
                    width: parent.width * 0.38
                    height: 510
                    radius: 30
                    color: "#111217"
                    border.width: 1
                    border.color: Qt.rgba(1,1,1,0.14)
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: "../assets/extensions/store/featured/thepiratebay-feature-bg.jpg"
                        fillMode: Image.PreserveAspectCrop
                        horizontalAlignment: Image.AlignHCenter
                        verticalAlignment: Image.AlignVCenter
                        opacity: 0.42
                        smooth: true
                        mipmap: true
                    }

                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0.00; color: Qt.rgba(7/255,8/255,11/255,0.12) }
                            GradientStop { position: 0.58; color: Qt.rgba(7/255,8/255,11/255,0.42) }
                            GradientStop { position: 1.00; color: Qt.rgba(7/255,8/255,11/255,0.96) }
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.leftMargin: 28
                        anchors.topMargin: 28
                        width: 78
                        height: 78
                        radius: 18
                        color: "#17181d"
                        border.width: 1
                        border.color: Qt.rgba(1,1,1,0.18)

                        Image {
                            anchors.fill: parent
                            anchors.margins: 5
                            source: "../assets/extensions/store/featured/thepiratebay-logo.png"
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: true
                        }
                    }

                    Rectangle {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.rightMargin: 28
                        anchors.topMargin: 28
                        width: featuredBadge.implicitWidth + 24
                        height: 36
                        radius: 18
                        color: Qt.rgba(7/255,8/255,12/255,0.62)
                        border.width: 1
                        border.color: Qt.rgba(1,1,1,0.14)

                        Text {
                            id: featuredBadge
                            anchors.centerIn: parent
                            text: "Featured extension"
                            color: Qt.rgba(1,1,1,0.76)
                            font.family: root.uiFamily
                            font.pixelSize: 11
                        }
                    }

                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 30
                        spacing: 11

                        Text {
                            text: "ThePirateBay+"
                            color: root.ivory
                            font.family: root.displayFamily
                            font.pixelSize: 42
                            font.weight: Font.Medium
                        }

                        Text {
                            width: parent.width
                            text: "Search for movies, series and anime from ThePirateBay"
                            color: Qt.rgba(1,1,1,0.72)
                            font.family: root.uiFamily
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            text: "Movies · TV · Anime   ·   ★ 526   ·   v1.4.0"
                            color: Qt.rgba(1,1,1,0.47)
                            font.family: root.uiFamily
                            font.pixelSize: 11
                        }

                        Row {
                            spacing: 10
                            topPadding: 8

                            Rectangle {
                                width: featureActionText.implicitWidth + 34
                                height: 44
                                radius: 14
                                color: root.ivory
                                Text {
                                    id: featureActionText
                                    anchors.centerIn: parent
                                    text: "View extension"
                                    color: "#111217"
                                    font.family: root.uiFamily
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.essentialRequested("thepiratebay+")
                                }
                            }

                            Rectangle {
                                width: saveText.implicitWidth + 30
                                height: 44
                                radius: 14
                                color: Qt.rgba(1,1,1,0.04)
                                border.width: 1
                                border.color: Qt.rgba(1,1,1,0.13)
                                Text {
                                    id: saveText
                                    anchors.centerIn: parent
                                    text: "Save"
                                    color: root.ivory
                                    font.family: root.uiFamily
                                    font.pixelSize: 14
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Qt.rgba(1,1,1,0.07)
            }

            Item { width: 1; height: 44 }

            Item {
                width: parent.width
                height: 54

                Text {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    text: "Essentials"
                    color: root.ivory
                    font.family: root.displayFamily
                    font.pixelSize: 31
                    font.weight: Font.Medium
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 152
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 5
                    text: "Four common picks for a fresh Colosseum setup."
                    color: Qt.rgba(1,1,1,0.45)
                    font.family: root.uiFamily
                    font.pixelSize: 12
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: -14
                    height: 1
                    color: Qt.rgba(1,1,1,0.07)
                }
            }

            Item { width: 1; height: 30 }

            Grid {
                id: essentialsGrid
                width: parent.width
                columns: 2
                columnSpacing: 14
                rowSpacing: 14

                EssentialCard {
                    width: (essentialsGrid.width - essentialsGrid.columnSpacing) / 2
                    title: "Torrentio"
                    slug: "torrentio"
                    stars: "2,184"
                    actionText: "Set up"
                    description: "Torrent streams from a wide range of providers, with optional debrid configuration."
                    iconSource: "../assets/extensions/store/essentials/torrentio.png"
                }

                EssentialCard {
                    width: (essentialsGrid.width - essentialsGrid.columnSpacing) / 2
                    title: "Streaming Catalogs"
                    slug: "streaming-catalogs"
                    stars: "781"
                    actionText: "Set up"
                    description: "Trending catalogues from Netflix, HBO Max, Disney+, Prime Video, Apple TV+ and more."
                    iconSource: "../assets/extensions/store/essentials/streaming-catalogs.png"
                }

                EssentialCard {
                    width: (essentialsGrid.width - essentialsGrid.columnSpacing) / 2
                    title: "OpenSubtitles v3"
                    slug: "opensubtitles-v3"
                    stars: "63"
                    actionText: "Get"
                    description: "The familiar OpenSubtitles provider for movie and series subtitles."
                    iconSource: "../assets/extensions/store/essentials/opensubtitles-v3.png"
                }

                EssentialCard {
                    width: (essentialsGrid.width - essentialsGrid.columnSpacing) / 2
                    title: "Anime Kitsu"
                    slug: "anime-kitsu"
                    stars: "118"
                    actionText: "Get"
                    description: "Kitsu-powered anime catalogues, metadata and subtitle support."
                    iconSource: "../assets/extensions/store/essentials/anime-kitsu.png"
                }
            }

            Item { width: 1; height: 70 }

            Text {
                text: "Explore the Store"
                color: root.ivory
                font.family: root.displayFamily
                font.pixelSize: 42
                font.weight: Font.Medium
            }

            Text {
                width: parent.width
                text: "Six wide doors into the catalogue. Each opens its own category page."
                color: Qt.rgba(1,1,1,0.48)
                font.family: root.uiFamily
                font.pixelSize: 13
                topPadding: 8
            }

            Item { width: 1; height: 30 }

            ListModel {
                id: categoryModel
                ListElement { modelTitle: "Movies"; modelCategory: "movies"; modelSubtitle: "Film sources, discovery and playback extensions."; modelExtensionCount: 355; modelAccent: "#8b5b34"; modelAccent2: "#473c59"; modelArtIcon: "../assets/icons/movies.svg" }
                ListElement { modelTitle: "TV Shows"; modelCategory: "tv+shows"; modelSubtitle: "Series, seasons and episodic libraries."; modelExtensionCount: 333; modelAccent: "#3e667c"; modelAccent2: "#3b4665"; modelArtIcon: "../assets/icons/feria-tv.svg" }
                ListElement { modelTitle: "Metadata"; modelCategory: "metadata"; modelSubtitle: "Posters, ratings, IDs and richer title information."; modelExtensionCount: 208; modelAccent: "#60527a"; modelAccent2: "#354b5c"; modelArtIcon: "../assets/icons/lucide/info.svg" }
                ListElement { modelTitle: "Anime"; modelCategory: "anime"; modelSubtitle: "Anime catalogues, metadata and playback sources."; modelExtensionCount: 192; modelAccent: "#864861"; modelAccent2: "#483b68"; modelArtIcon: "../assets/icons/manga.svg" }
                ListElement { modelTitle: "Asian Drama"; modelCategory: "asian+drama"; modelSubtitle: "Korean, Chinese, Japanese and wider Asian drama."; modelExtensionCount: 99; modelAccent: "#79614d"; modelAccent2: "#405a66"; modelArtIcon: "../assets/icons/lucide/languages.svg" }
                ListElement { modelTitle: "Bollywood"; modelCategory: "bollywood"; modelSubtitle: "Hindi cinema and Indian-film focused extensions."; modelExtensionCount: 90; modelAccent: "#8c4f33"; modelAccent2: "#63384a"; modelArtIcon: "../assets/icons/star.svg" }
            }

            Column {
                id: categoryColumn
                width: parent.width
                spacing: 22

                Repeater {
                    model: categoryModel
                    delegate: CategoryBanner {
                        required property string modelTitle
                        required property string modelCategory
                        required property string modelSubtitle
                        required property int modelExtensionCount
                        required property color modelAccent
                        required property color modelAccent2
                        required property string modelArtIcon

                        width: categoryColumn.width
                        title: modelTitle
                        category: modelCategory
                        subtitle: modelSubtitle
                        extensionCount: modelExtensionCount
                        accent: modelAccent
                        accent2: modelAccent2
                        artIcon: modelArtIcon
                    }
                }
            }

            Item { width: 1; height: 46 }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignRight
                text: "Catalogue data from Stremio Addons"
                color: Qt.rgba(1,1,1,0.30)
                font.family: root.uiFamily
                font.pixelSize: 9
            }
        }
    }

    Rectangle {
        id: topPicker
        z: 50
        x: (root.width - width) / 2
        y: 31
        width: 380
        height: 60
        radius: 30
        color: Qt.rgba(105/255,105/255,105/255,0.24)
        border.width: 1
        border.color: Qt.rgba(1,1,1,0.07)

        Row {
            anchors.fill: parent
            anchors.margins: 7
            spacing: 4

            Repeater {
                model: ["Chain", "House", "Store"]
                delegate: Rectangle {
                    id: modeButton
                    required property string modelData
                    width: 118
                    height: parent.height
                    radius: height / 2
                    color: modelData === "Store"
                           ? root.gold
                           : (modeMouse.containsMouse ? Qt.rgba(1,1,1,0.07) : "transparent")
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData

                    Text {
                        anchors.centerIn: parent
                        text: modeButton.modelData
                        color: modeButton.modelData === "Store" ? "#15120b" : Qt.rgba(1,1,1,0.70)
                        font.family: root.uiFamily
                        font.pixelSize: 17
                        font.weight: modeButton.modelData === "Store" ? Font.DemiBold : Font.Normal
                    }

                    MouseArea {
                        id: modeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            modeButton.forceActiveFocus(Qt.MouseFocusReason)
                            if (modeButton.modelData !== "Store")
                                root.sectionRequested(modeButton.modelData.toLowerCase())
                        }
                    }
                }
            }
        }
    }

    TextField {
        id: searchField
        z: 50
        anchors.right: parent.right
        anchors.rightMargin: 42
        y: 38
        width: root.width > 1380 ? 360 : 280
        height: 46
        placeholderText: "Search extensions"
        color: root.ivory
        placeholderTextColor: Qt.rgba(1,1,1,0.38)
        font.family: root.uiFamily
        font.pixelSize: 13
        leftPadding: 16
        rightPadding: 42
        selectByMouse: true
        background: Rectangle {
            radius: 15
            color: Qt.rgba(17/255,18/255,24/255,0.82)
            border.width: 1
            border.color: searchField.activeFocus
                          ? Qt.rgba(240/255,193/255,90/255,0.52)
                          : Qt.rgba(1,1,1,0.12)
        }
        onAccepted: root.searchRequested(text.trim())
    }
}
