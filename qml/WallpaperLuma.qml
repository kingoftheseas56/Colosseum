// WallpaperLuma — how bright the current wallpaper image is (0 = black, 1 = white), so the backdrop
// vignette can deepen on bright picks (parchment, snow) and leave dark ones exactly as they are.
// Draws the image once into a 32x18 canvas per wallpaper change, averages the luma, then drops the
// full-size decode — nothing runs between changes. Native QML scenes are not measured (luma stays 0);
// they were all designed against the baseline vignette.
import QtQuick

Canvas {
    id: root
    property url source: ""
    property real luma: 0
    width: 32
    height: 18
    renderStrategy: Canvas.Immediate

    // luma holds its old value until the new image is measured, so bright-to-bright swaps don't flicker.
    onSourceChanged: {
        if (source.toString().length > 0) loadImage(source)
        else luma = 0
    }
    onImageLoaded: requestPaint()
    onPaint: {
        var src = source.toString()
        if (!src.length || !isImageLoaded(src)) return
        var ctx = getContext("2d")
        ctx.clearRect(0, 0, width, height)
        ctx.drawImage(src, 0, 0, width, height)
        var d = ctx.getImageData(0, 0, width, height).data
        var sum = 0
        for (var i = 0; i < d.length; i += 4)
            sum += 0.2126 * d[i] + 0.7152 * d[i + 1] + 0.0722 * d[i + 2]
        luma = sum / (d.length / 4) / 255
        unloadImage(src)
    }
}
