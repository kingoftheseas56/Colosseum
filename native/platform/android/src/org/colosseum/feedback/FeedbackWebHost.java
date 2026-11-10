package org.colosseum.feedback;

import android.content.Context;
import android.net.http.SslError;
import android.view.KeyEvent;
import android.view.ViewGroup;
import android.webkit.PermissionRequest;
import android.webkit.RenderProcessGoneDetail;
import android.webkit.SslErrorHandler;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import androidx.webkit.WebViewAssetLoader;
import java.io.ByteArrayInputStream;

/** A disposable feedback or support page with no JavaScript-to-native bridge. */
public final class FeedbackWebHost {
    private final WebView view;
    private long nativeId;
    private boolean failed;
    private static native void onEvent(long instance, String event);

    public FeedbackWebHost(Context context, long instance, String url, boolean bundledSupport) {
        nativeId = instance;
        view = new WebView(context);
        final boolean support = FeedbackNavigationPolicy.supportDocument(url);
        final WebViewAssetLoader assets = bundledSupport ? new WebViewAssetLoader.Builder()
            .addPathHandler("/assets/", new WebViewAssetLoader.AssetsPathHandler(context))
            .build() : null;
        view.setBackgroundColor(0xFF08090D);
        WebSettings settings = view.getSettings();
        settings.setJavaScriptEnabled(!support);
        settings.setDomStorageEnabled(!support);
        if (support) settings.setUseWideViewPort(true);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        settings.setSupportMultipleWindows(false);
        settings.setJavaScriptCanOpenWindowsAutomatically(false);
        view.setDownloadListener((uri, agent, disposition, mime, size) -> { });
        view.setWebChromeClient(new WebChromeClient() {
            @Override public void onPermissionRequest(PermissionRequest request) { request.deny(); }
        });
        // Android Back belongs to ColosseumActivity's controller.
        view.setOnKeyListener((v, code, event) -> {
            if (code == KeyEvent.KEYCODE_ESCAPE
                && event.getAction() == KeyEvent.ACTION_UP) { emit("close"); return true; }
            return false;
        });
        view.setWebViewClient(new WebViewClient() {
            private boolean supportDocument(String destination) {
                return bundledSupport ? FeedbackNavigationPolicy.BUNDLED_SUPPORT.equals(destination)
                    : FeedbackNavigationPolicy.supportDocument(destination);
            }
            @Override public boolean shouldOverrideUrlLoading(WebView web, WebResourceRequest request) {
                final String destination = request.getUrl().toString();
                if (!support) return request.isForMainFrame() && !FeedbackNavigationPolicy.document(destination);
                if (!request.isForMainFrame()) return true;
                if (supportDocument(destination)) return false;
                if (request.hasGesture() && !request.isRedirect()) emit("external:" + destination);
                else if (!bundledSupport && request.isRedirect()) error();
                return true;
            }
            @Override public WebResourceResponse shouldInterceptRequest(WebView web, WebResourceRequest request) {
                final String destination = request.getUrl().toString();
                if (support && bundledSupport && FeedbackNavigationPolicy.bundledSupportResource(destination)) {
                    final WebResourceResponse asset = assets.shouldInterceptRequest(request.getUrl());
                    if (asset != null) return asset;
                } else if (support ? !bundledSupport && FeedbackNavigationPolicy.supportResource(destination)
                                   : FeedbackNavigationPolicy.resource(destination)) {
                    return null;
                }
                return new WebResourceResponse("text/plain", "UTF-8", 403, "Blocked", null, new ByteArrayInputStream(new byte[0]));
            }
            @Override public void onPageFinished(WebView web, String url) {
                if (!failed && (!support || supportDocument(url))) emit("loaded");
            }
            @Override public void doUpdateVisitedHistory(WebView web, String url, boolean isReload) {
                if (FeedbackNavigationPolicy.closesReport(url)) emit("close");
            }
            @Override public void onReceivedError(WebView web, WebResourceRequest request, WebResourceError error) {
                if (request.isForMainFrame()) error();
            }
            @Override public void onReceivedHttpError(WebView web, WebResourceRequest request, WebResourceResponse response) {
                if (request.isForMainFrame()) error();
            }
            @Override public void onReceivedSslError(WebView web, SslErrorHandler handler, SslError error) {
                handler.cancel(); error();
            }
            @Override public boolean onRenderProcessGone(WebView web, RenderProcessGoneDetail detail) { error(); return true; }
        });
        if (support) view.loadUrl(bundledSupport ? FeedbackNavigationPolicy.BUNDLED_SUPPORT : url);
        else if (!bundledSupport && FeedbackNavigationPolicy.document(url)) view.loadUrl(url);
        else error();
    }
    private void emit(String event) { if (nativeId != 0) onEvent(nativeId, event); }
    private void error() { failed = true; emit("error"); }
    public WebView view() { return view; }
    public void setActive(boolean active) { if (nativeId == 0) return; if (active) view.onResume(); else view.onPause(); }
    public void destroy() {
        nativeId = 0;
        view.stopLoading();
        if (view.getParent() instanceof ViewGroup) ((ViewGroup)view.getParent()).removeView(view);
        view.destroy();
    }
}
