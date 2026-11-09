package org.colosseum.feedback;

import java.net.URI;

public final class FeedbackNavigationPolicy {
    private static final String ASSET_ROOT = "https://appassets.androidplatform.net/assets/colosseum-runtime/assets/";
    public static final String BUNDLED_SUPPORT = ASSET_ROOT + "support/index.html";

    private FeedbackNavigationPolicy() { }

    public static boolean supportDocument(String value) {
        try {
            URI uri = new URI(value);
            return trustedSite(uri) && ("/support/app".equals(uri.getRawPath()) || "/support/app/".equals(uri.getRawPath()));
        } catch (Exception ignored) { return false; }
    }

    public static boolean supportResource(String value) {
        try {
            return trustedSite(new URI(value));
        } catch (Exception ignored) { return false; }
    }

    public static boolean bundledSupportResource(String value) {
        return BUNDLED_SUPPORT.equals(value)
            || (ASSET_ROOT + "support/support.css").equals(value)
            || (ASSET_ROOT + "support/fonts/editorial.otf").equals(value)
            || (ASSET_ROOT + "support/fonts/body.ttf").equals(value)
            || (ASSET_ROOT + "support/fonts/body-semibold.ttf").equals(value)
            || (ASSET_ROOT + "wallpaper/cold-ripple.jpg").equals(value);
    }

    public static boolean document(String value) {
        try {
            URI uri = new URI(value);
            String path = uri.getPath();
            return trustedSite(uri) && ("/feedback".equals(path) || "/feedback/".equals(path)
                || "/feedback/index.html".equals(path) || reportPath(path));
        } catch (Exception ignored) { return false; }
    }
    public static boolean closesReport(String value) {
        try {
            URI uri = new URI(value);
            return trustedSite(uri) && reportPath(uri.getPath())
                && "close-feedback".equals(uri.getRawFragment());
        } catch (Exception ignored) { return false; }
    }
    private static boolean reportPath(String path) {
        return "/feedback/report".equals(path) || "/feedback/report.html".equals(path);
    }
    public static boolean resource(String value) {
        if ("about:blank".equals(value) || "about:srcdoc".equals(value)) return true;
        try {
            URI uri = new URI(value);
            if (!"https".equals(uri.getScheme()) || uri.getHost() == null || uri.getRawUserInfo() != null
                || (uri.getPort() != -1 && uri.getPort() != 443)) return false;
            // The owned page's CSP controls HTTPS resource origins. Keep provider changes
            // in that hosted policy; only top-level documents are pinned in the app.
            return true;
        } catch (Exception ignored) { return false; }
    }
    private static boolean trustedSite(URI uri) {
        return "https".equals(uri.getScheme()) && "getcolosseum.world".equals(uri.getHost())
            && uri.getRawUserInfo() == null && (uri.getPort() == -1 || uri.getPort() == 443);
    }
}
