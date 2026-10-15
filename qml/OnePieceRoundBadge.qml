import QtQuick

// Circular canvas clipping works in both software and hardware renderers.
Item {
    id: root
    property alias source: decoded.source
    property alias sourceSize: decoded.sourceSize
    property alias asynchronous: decoded.asynchronous
    property alias cache: decoded.cache
    property alias mipmap: decoded.mipmap
    property alias fillMode: decoded.fillMode
    readonly property int status: decoded.status === Image.Ready && !paint.imageReady ? Image.Loading : decoded.status

    Image {
        id: decoded
        objectName: "onePieceRoundBadgeDecoded"
        visible: false
        asynchronous: true
        cache: true
        smooth: root.smooth
        fillMode: Image.PreserveAspectCrop
        sourceSize: Qt.size(Math.ceil(root.width * 2), Math.ceil(root.height * 2))
        onStatusChanged: paint.requestPaint()
        onImplicitWidthChanged: paint.requestPaint()
        onImplicitHeightChanged: paint.requestPaint()
    }
    Canvas {
        id: paint
        objectName: "onePieceRoundBadgeCanvas"
        anchors.fill: parent
        canvasSize: Qt.size(Math.max(1, Math.ceil(width)), Math.max(1, Math.ceil(height)))
        canvasWindow: Qt.rect(0, 0, canvasSize.width, canvasSize.height)
        property url imageSource: String(root.source).length ? Qt.resolvedUrl(root.source) : ""
        property url previousSource: ""
        property bool imageReady: false
        function loadSource() {
            if (!available || !String(imageSource).length) return
            loadImage(imageSource)
            // A cache hit can complete without another imageLoaded signal.
            imageReady = isImageLoaded(imageSource)
            requestPaint()
        }
        onImageSourceChanged: {
            imageReady = false
            if (String(previousSource).length) unloadImage(previousSource)
            previousSource = imageSource
            loadSource()
            requestPaint()
        }
        onAvailableChanged: loadSource()
        onImageLoaded: {
            imageReady = isImageLoaded(imageSource)
            requestPaint()
        }
        onCanvasSizeChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, canvasSize.width, canvasSize.height)
            if (decoded.status !== Image.Ready || !isImageLoaded(imageSource)) return
            var w = canvasSize.width, h = canvasSize.height
            var imageW = decoded.implicitWidth, imageH = decoded.implicitHeight
            if (imageW <= 0 || imageH <= 0) return
            var scale = Math.max(w / imageW, h / imageH)
            ctx.beginPath()
            ctx.arc(w / 2, h / 2, Math.max(0, Math.min(w, h) / 2 - 1), 0, Math.PI * 2)
            ctx.clip()
            ctx.drawImage(String(imageSource), (w - imageW * scale) / 2, (h - imageH * scale) / 2,
                          imageW * scale, imageH * scale)
        }
    }
}
