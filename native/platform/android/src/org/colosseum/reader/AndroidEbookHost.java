package org.colosseum.reader;

import android.content.Context;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.view.ViewGroup;
import android.webkit.RenderProcessGoneDetail;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceError;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import androidx.webkit.WebViewAssetLoader;
import androidx.webkit.WebViewCompat;
import androidx.webkit.WebViewFeature;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayInputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

/** Android UI-thread owner of one disposable EPUB WebView. Reader2 owns all durable state. */
public final class AndroidEbookHost {
    private static final int MAX_MESSAGE_CHARS = 512 * 1024;
    private final Context context;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ReaderPublicationSession session = new ReaderPublicationSession();
    private final WebView view;
    private long nativeId;
    private boolean ready;
    private boolean destroyed;

    private static native void onEvent(long instance, String event, String payload);

    public AndroidEbookHost(Context context, long instance) {
        this.context = context;
        nativeId = instance;
        view = new WebView(context);
        view.setBackgroundColor(0xFF09090B); // the shell's own background; no white flash before it loads
        WebSettings settings = view.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setAllowFileAccessFromFileURLs(false);
        settings.setAllowUniversalAccessFromFileURLs(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        settings.setDomStorageEnabled(false);
        settings.setSupportMultipleWindows(false);
        settings.setJavaScriptCanOpenWindowsAutomatically(false);
        settings.setCacheMode(WebSettings.LOAD_NO_CACHE);
        view.setDownloadListener((url, agent, disposition, mime, size) -> { });
        if (!WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER)) {
            ui.post(() -> error(0, "incompatible_webview", "Update Android System WebView to read EPUB books."));
            return;
        }

        WebViewAssetLoader loader = new WebViewAssetLoader.Builder()
            .addPathHandler("/assets/", new WebViewAssetLoader.AssetsPathHandler(context))
            .addPathHandler("/publication/", this::publicationResponse)
            .build();
        view.setWebViewClient(new WebViewClient() {
            @Override public void onReceivedError(WebView web, WebResourceRequest request, WebResourceError failure) {
                if (request.isForMainFrame())
                    error(session.generation(), "renderer_load_failed", "The EPUB reader could not load. Close and reopen the book.");
            }

            @Override public void onReceivedHttpError(WebView web, WebResourceRequest request, WebResourceResponse response) {
                if (request.isForMainFrame())
                    error(session.generation(), "renderer_load_failed", "The EPUB reader assets are unavailable.");
            }

            @Override public WebResourceResponse shouldInterceptRequest(WebView web, WebResourceRequest request) {
                Uri uri = request.getUrl();
                if (!"GET".equals(request.getMethod()) || !"https".equals(uri.getScheme())
                    || !"appassets.androidplatform.net".equals(uri.getEncodedAuthority())) return denied();
                String path = uri.getPath();
                if (path == null || !(path.startsWith("/assets/reader2/") || path.startsWith("/publication/")))
                    return denied();
                WebResourceResponse response = loader.shouldInterceptRequest(uri);
                if (response == null) return denied();
                Map<String, String> headers = new HashMap<>();
                headers.put("Cache-Control", "no-store");
                headers.put("X-Content-Type-Options", "nosniff");
                headers.put("Content-Security-Policy", "default-src 'none'; script-src 'self'; "
                    + "style-src 'self' 'unsafe-inline' blob:; img-src 'self' blob: data:; "
                    + "font-src 'self' blob: data:; connect-src 'self'; frame-src blob:; "
                    + "worker-src 'self' blob:; media-src 'none'; object-src 'none'; base-uri 'none'");
                response.setResponseHeaders(headers);
                return response;
            }

            @Override public boolean shouldOverrideUrlLoading(WebView web, WebResourceRequest request) {
                // The host alone loads the trusted shell. Publication links cannot replace it.
                return request.isForMainFrame() || !"blob".equals(request.getUrl().getScheme());
            }

            @Override public boolean onRenderProcessGone(WebView web, RenderProcessGoneDetail detail) {
                ready = false;
                long generation = session.generation();
                session.close();
                error(generation, "renderer_lost", "The book renderer stopped. Close and reopen the book.");
                emit("rendererLost", "{}");
                return true; // Qt releases the foreign wrapper before it asks us to destroy the view.
            }
        });
        WebViewCompat.addWebMessageListener(view, "ColosseumPaper",
            Collections.singleton(ReaderPublicationSession.ORIGIN), (web, message, origin, mainFrame, reply) -> {
                String data = message.getData();
                if (destroyed || data == null || data.length() > MAX_MESSAGE_CHARS) return;
                try {
                    JSONObject envelope = new JSONObject(data);
                    String event = envelope.getString("event");
                    JSONObject payload = envelope.getJSONObject("payload");
                    Object generationValue = envelope.get("generation");
                    if (!(generationValue instanceof Number)) return;
                    double numeric = ((Number) generationValue).doubleValue();
                    long generation = ((Number) generationValue).longValue();
                    if (numeric != generation || generation < 0) return;
                    if (!session.acceptsEvent(origin.toString(), mainFrame, web.getUrl(), event, generation)) return;
                    if (payload.has("gen") && payload.getDouble("gen") != generation) return;
                    if ("glueLoaded".equals(event)) ready = true;
                    if (generation > 0) payload.put("gen", generation);
                    emit(event, payload.toString());
                } catch (JSONException ignored) { /* malformed messages cannot mutate native state */ }
            });
        view.loadUrl(ReaderPublicationSession.SHELL_URL);
    }

    public WebView view() { return view; }

    public void open(String source, String cfi, long generation) {
        if (destroyed || !ready) {
            error(generation, "renderer_unavailable", "The EPUB renderer is not ready.");
            return;
        }
        try {
            ReaderPublicationSession.Publication publication = session.open(source, generation);
            dispatch("open", new JSONArray().put(publication.url).put(cfi).put(generation));
        } catch (IllegalArgumentException error) {
            error(generation, "unsupported_source", "This EPUB source cannot be opened.");
        }
    }

    public void command(String name, String arguments) {
        if (destroyed || !ready) return;
        if ("focusPaper".equals(name)) { view.requestFocus(); return; }
        if (name.contains("ReadAlong")) {
            error(session.generation(), "unsupported_feature", "Read-along is deferred on Android.");
            return;
        }
        try { dispatch(name, new JSONArray(arguments)); }
        catch (JSONException error) { error(session.generation(), "invalid_command", "Invalid reader command."); }
    }

    private void dispatch(String name, JSONArray args) {
        if (!ReaderPublicationSession.isShellUrl(view.getUrl())) return;
        try {
            JSONObject command = new JSONObject().put("name", name).put("args", args);
            view.evaluateJavascript("window.androidPaperDispatch(JSON.parse(" + JSONObject.quote(command.toString()) + "))", null);
        } catch (JSONException error) { error(session.generation(), "invalid_command", "Invalid reader command."); }
    }

    private WebResourceResponse publicationResponse(String path) {
        ReaderPublicationSession.Publication publication = session.resolve(path);
        if (publication == null) return denied();
        try {
            Uri uri = Uri.parse(publication.source);
            InputStream stream = "content".equals(uri.getScheme())
                ? context.getContentResolver().openInputStream(uri)
                : new FileInputStream(uri.getPath());
            if (stream == null) throw new IOException("No stream");
            return new WebResourceResponse("application/epub+zip", null, stream);
        } catch (SecurityException error) {
            sourceError(publication, "permission_required", "Access to this book must be granted again.");
        } catch (IOException error) {
            sourceError(publication, "source_unavailable", "The book is missing or its document provider is offline.");
        }
        return denied();
    }

    private void sourceError(ReaderPublicationSession.Publication publication, String code, String message) {
        ui.post(() -> {
            if (session.resolve(publication.path) == publication) error(publication.generation, code, message);
        });
    }

    private static WebResourceResponse denied() {
        return new WebResourceResponse("text/plain", "UTF-8", 403, "Forbidden",
            Collections.singletonMap("Cache-Control", "no-store"), new ByteArrayInputStream(new byte[0]));
    }

    private void error(long generation, String code, String message) {
        try { emit("error", new JSONObject().put("gen", generation).put("code", code).put("message", message).toString()); }
        catch (JSONException ignored) { }
    }
    private void emit(String event, String payload) {
        if (!destroyed && nativeId != 0) onEvent(nativeId, event, payload);
    }
    public void setActive(boolean active) {
        if (destroyed) return;
        if (active) view.onResume(); else view.onPause();
    }
    public void destroy() {
        if (destroyed) return;
        destroyed = true;
        ready = false;
        nativeId = 0;
        session.close();
        view.stopLoading();
        if (view.getParent() instanceof ViewGroup) ((ViewGroup) view.getParent()).removeView(view);
        view.destroy();
    }
}
