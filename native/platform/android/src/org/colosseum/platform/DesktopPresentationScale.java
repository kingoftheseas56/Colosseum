package org.colosseum.platform;

/** One startup scale for the entire desktop canvas, independent of fonts and the IME. */
public final class DesktopPresentationScale {
    private DesktopPresentationScale() {}

    // A display that already fits the desktop canvas needs no additional magnification.
    // Smaller displays retain the installed phone composition after fitting that canvas.
    public static float defaultVisualZoom(float baselineScale) {
        return baselineScale >= 1f ? 1f : 1.5f;
    }

    public static float forBounds(int width, int height, float density) {
        if (width <= 0 || height <= 0 || !Float.isFinite(density) || density <= 0) return 1f;
        double limit = Math.min(width / (density * 1024.0), height / (density * 640.0));
        if (limit >= 1) return 1f;
        // Qt consumes a floating-point factor. Round down so integer viewport geometry
        // cannot fall one pixel below the minimum after serialization and parsing.
        return Math.nextDown((float) limit);
    }
}
