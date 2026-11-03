package org.colosseum.feedback;

import java.net.URI;

public final class FeedbackNavigationPolicy {
    private FeedbackNavigationPolicy() { }
    public static boolean document(String value) {
        try {
            URI uri = new URI(value);
            String path = uri.getPath();
            return trustedSite(uri) && ("/feedback".equals(path) || "/feedback/".equals(path)
                || "/feedback/index.html".equals(path));
        } catch (Exception ignored) { return false; }
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
