import QtQuick 2.15
import QtQuick.Controls.Basic 2.15 as Basic

Item {
    id: root
    objectName: "guideArticle"
    property var lesson: null
    property var catalog: null
    property bool reducedMotion: false
    readonly property var supportedKinds: ["paragraph", "steps", "bullets", "note", "image"]
    property var renderedKinds: []
    property string visibleText: ""
    property var relatedLessons: []
    signal relatedRequested(string lessonId)

    function blockText(block) {
        if (!block) return ""
        if (block.kind === "steps" || block.kind === "bullets") return Array.isArray(block.items) ? block.items.join(" ") : ""
        return String(block.text || block.alt || "")
    }

    function canRenderImage(block) {
        var path = String(block && block.path || "").trim()
        var alt = String(block && block.alt || "").trim()
        var lowerPath = path.toLowerCase()
        return path.length > 0 && alt.length > 0
                && lowerPath.indexOf("://") < 0
                && lowerPath.indexOf("http:") !== 0
                && lowerPath.indexOf("https:") !== 0
                && lowerPath.indexOf("ftp:") !== 0
                && lowerPath.indexOf("data:") !== 0
    }

    function refresh() {
        var kinds = []
        var text = []
        var blocks = lesson && Array.isArray(lesson.blocks) ? lesson.blocks : []
        for (var i = 0; i < blocks.length; ++i) {
            var block = blocks[i]
            if (!block || supportedKinds.indexOf(block.kind) < 0) {
                console.warn("GuideArticle unknown block kind for lesson " + (lesson ? lesson.id : "<missing>"))
                continue
            }
            if (block.kind === "image" && !canRenderImage(block))
                console.warn("GuideArticle unusable image block for lesson " + (lesson ? lesson.id : "<missing>"))
            kinds.push(block.kind)
            text.push(blockText(block))
        }
        renderedKinds = kinds
        visibleText = text.join(" ")
        var related = []
        if (lesson && Array.isArray(lesson.related)) {
            for (var j = 0; j < lesson.related.length; ++j) {
                var id = lesson.related[j]
                var item = catalog && catalog.find ? catalog.find(id) : null
                if (item) related.push(item)
                else console.warn("GuideArticle invalid related lesson for " + lesson.id + ": " + id)
            }
        }
        relatedLessons = related
    }

    onLessonChanged: refresh()
    onCatalogChanged: refresh()

    Column {
        id: articleContent
        width: parent.width
        spacing: 13
        Text { text: root.lesson ? String(root.lesson.section || "").toUpperCase() : ""; color: "#969696"; font.pixelSize: 11; font.letterSpacing: 1.1 }
        Text { text: root.lesson ? root.lesson.title : ""; color: "#f4f4f4"; width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 28; font.weight: Font.DemiBold }
        Repeater {
            model: root.lesson && Array.isArray(root.lesson.blocks) ? root.lesson.blocks : []
            delegate: Item {
                readonly property var block: modelData
                visible: block && root.supportedKinds.indexOf(block.kind) >= 0
                width: parent.width
                implicitHeight: blockItem.implicitHeight
                Loader {
                    id: blockItem
                    width: parent.width
                    sourceComponent: {
                        if (!block) return emptyBlock
                        if (block.kind === "paragraph" || block.kind === "note") return textBlock
                        if (block.kind === "steps" || block.kind === "bullets") return listBlock
                        if (block.kind === "image") return root.canRenderImage(block) ? imageBlock : imageFallbackBlock
                        return emptyBlock
                    }
                }
                Component {
                    id: emptyBlock
                    Item { implicitHeight: 0 }
                }
                Component {
                    id: textBlock
                    Text { text: String(block.text || ""); color: block.kind === "note" ? "#dedede" : "#c8c8c8"; width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 15; padding: block.kind === "note" ? 12 : 0 }
                }
                Component {
                    id: listBlock
                    Text { text: (Array.isArray(block.items) ? block.items : []).map(function(item, index) { return block.kind === "steps" ? (index + 1) + ". " + item : "• " + item }).join("\n"); color: "#c8c8c8"; width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 15; lineHeight: 1.35 }
                }
                Component {
                    id: imageBlock
                    Column {
                        width: parent.width
                        spacing: 6
                        Image { objectName: "guideArticleImageVisual"; visible: status === Image.Ready; source: Qt.resolvedUrl(block.path); width: Math.min(implicitWidth, parent.width); fillMode: Image.PreserveAspectFit }
                        Text { text: String(block.alt || ""); color: "#c8c8c8"; width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 14 }
                    }
                }
                Component {
                    id: imageFallbackBlock
                    Text { text: root.blockText(block); color: "#c8c8c8"; width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 14 }
                }
            }
        }
        Column {
            visible: root.relatedLessons.length > 0
            width: parent.width
            spacing: 5
            Text { text: "RELATED"; color: "#969696"; font.pixelSize: 11; font.letterSpacing: 1.1 }
            Repeater {
                model: root.relatedLessons
                delegate: Basic.Button { text: modelData.title; activeFocusOnTab: true; onClicked: root.relatedRequested(modelData.id) }
            }
        }
    }
}
