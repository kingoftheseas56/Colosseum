package org.colosseum.platform;

public final class DesktopPresentationScaleTest {
    private static void valid(int width, int height, float density, double expected) {
        double scale = DesktopPresentationScale.forBounds(width, height, density);
        if (!(scale > 0 && scale <= 1) || Math.abs(scale - expected) > 0.000001)
            throw new AssertionError("Unexpected scale " + scale);
        if (Math.floor(width / (density * scale)) < 1024
                || Math.floor(height / (density * scale)) < 640)
            throw new AssertionError("Desktop minimum violated");
    }

    public static void main(String[] args) {
        valid(2376, 1062, 3f, 0.553125); // OnePlus-like drawable landscape bounds
        valid(2560, 1600, 2f, 1);       // sufficient tablet keeps normal sizing
        valid(1920, 1080, 1.5f, 1);     // exact 1280x720 desktop
        valid(1024, 640, 1f, 1);        // exact minimum is not shrunk
        valid(1800, 1200, 2.625f, 0.6696428571428571);
        valid(1280, 400, 1f, 0.625);    // short landscape
        for (float density : new float[] {0, -1, Float.NaN, Float.POSITIVE_INFINITY})
            if (DesktopPresentationScale.forBounds(2400, 1080, density) != 1)
                throw new AssertionError("Invalid density must use safe fallback");
        if (DesktopPresentationScale.forBounds(0, 1080, 3) != 1
                || DesktopPresentationScale.forBounds(2400, -1, 3) != 1)
            throw new AssertionError("Invalid bounds must use safe fallback");
        System.out.println("DESKTOP_PRESENTATION_SCALE_OK");
    }
}
