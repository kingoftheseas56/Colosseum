// Native API feasibility probe. This does not instantiate Colosseum's owners.
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <chrono>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
static std::deque<std::function<void()>> deferred;
static std::string fatal;
static int passed = 0;
static bool negativeDetected = false;

static std::string utf8(const std::wstring &value)
{
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), length, nullptr, nullptr);
    return result;
}

static std::string quote(const std::string &text)
{
    std::string result = "\"";
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') { result += '\\'; result += c; }
        else if (c < 32) {
            const char hex[] = "0123456789abcdef";
            result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15];
        } else result += c;
    }
    return result + "\"";
}

static void log(const std::string &event, const std::string &detail)
{
    std::cout << "{\"event\":" << quote(event) << ",\"detail\":"
              << quote(detail) << "}" << std::endl;
}

static void require(bool ok, const std::string &description)
{
    log(ok ? "pass" : "fail", description);
    if (!ok) throw std::runtime_error(description);
    ++passed;
}

static void check(HRESULT hr, const char *operation)
{
    if (FAILED(hr)) throw std::runtime_error(std::string(operation) +
                                            " HRESULT=" + std::to_string(hr));
}

// Never throw through a COM callback. Async API failures wake the test pump.
static bool callbackOk(HRESULT hr, const char *operation)
{
    if (SUCCEEDED(hr)) return true;
    fatal = std::string(operation) + " HRESULT=" + std::to_string(hr);
    log("api-failure", fatal);
    return false;
}

static void waitFor(const std::function<bool()> &ready, const char *operation)
{
    const auto deadline = Clock::now() + std::chrono::seconds(15);
    while (!ready()) {
        if (!fatal.empty()) throw std::runtime_error(fatal);
        if (Clock::now() > deadline) throw std::runtime_error(std::string("timeout: ") + operation);
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) throw std::runtime_error("unexpected WM_QUIT");
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        while (!deferred.empty()) {
            auto task = std::move(deferred.front()); deferred.pop_front(); task();
        }
        Sleep(1);
    }
    if (!fatal.empty()) throw std::runtime_error(fatal);
}

struct Reply {
    bool done = false;
    HRESULT hr = E_PENDING;
    std::wstring text;
};

struct Navigation {
    UINT64 id = 0;
    std::wstring target;
    BOOL success = FALSE;
    COREWEBVIEW2_WEB_ERROR_STATUS error = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
    bool completed = false;
};

struct View {
    HWND window = nullptr;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;
    std::map<UINT64, Navigation> navigations;
    std::vector<UINT64> starts;
    int dialogs = 0;
    int denied = 0;
    int accepted = 0;
    std::wstring dialogUri;
    bool leave = false;
    std::string label;
    EventRegistrationToken starting{}, completed{}, dialog{};

    std::wstring source() const {
        LPWSTR text = nullptr;
        check(web->get_Source(&text), "get_Source");
        std::wstring result = text ? text : L""; CoTaskMemFree(text); return result;
    }

    std::wstring script(const wchar_t *javascript) {
        auto reply = std::make_shared<Reply>();
        check(web->ExecuteScript(javascript,
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [reply](HRESULT hr, LPCWSTR text) -> HRESULT {
                    reply->hr = hr; reply->text = text ? text : L"";
                    reply->done = true; return S_OK;
                }).Get()), "ExecuteScript dispatch");
        waitFor([reply] { return reply->done; }, "ExecuteScript completion");
        check(reply->hr, "ExecuteScript completion"); return reply->text;
    }

    std::wstring cdp(const wchar_t *method, const wchar_t *parameters) {
        auto reply = std::make_shared<Reply>();
        check(web->CallDevToolsProtocolMethod(method, parameters,
            Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
                [reply](HRESULT hr, LPCWSTR text) -> HRESULT {
                    reply->hr = hr; reply->text = text ? text : L"";
                    reply->done = true; return S_OK;
                }).Get()), "CDP dispatch");
        waitFor([reply] { return reply->done; }, "CDP completion");
        check(reply->hr, "CDP completion");
        log("cdp-reply", label + " " + utf8(method) + " " + utf8(reply->text));
        return reply->text;
    }

    void activate() {
        // Renderer-targeted trusted input: no shared desktop mouse/keyboard.
        // ExecuteScript itself grants activation in this runtime, even for a
        // read. Do not call it until trusted input has established activation.
        log("activation-before", utf8(cdp(L"Runtime.evaluate", L"{\"expression\":\"snapshot()\",\"returnByValue\":true,\"userGesture\":false}")));
        check(controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC), "MoveFocus");
        cdp(L"Input.dispatchMouseEvent", L"{\"type\":\"mousePressed\",\"x\":50,\"y\":40,\"button\":\"left\",\"clickCount\":1}");
        cdp(L"Input.dispatchMouseEvent", L"{\"type\":\"mouseReleased\",\"x\":50,\"y\":40,\"button\":\"left\",\"clickCount\":1}");
        log("activation-after", utf8(script(L"snapshot()")));
        require(script(L"trustedClicks === 1 && navigator.userActivation.hasBeenActive && draft.value === 'unsaved work'") == L"true",
                label + ": real user activation and unsaved form established");
    }

    Navigation navigate(const std::wstring &uri) {
        const auto count = starts.size();
        check(web->Navigate(uri.c_str()), "Navigate dispatch");
        waitFor([this, count] { return starts.size() > count; }, "NavigationStarting");
        const auto id = starts[count];
        waitFor([this, id] { return navigations.at(id).completed; }, "NavigationCompleted");
        const auto result = navigations.at(id);
        require(result.target == uri, label + ": terminal callback belongs to requested navigation");
        return result;
    }

    void close() {
        if (!controller) return;
        check(controller->Close(), "controller Close");
        controller.Reset(); web.Reset();
    }
};

static std::unique_ptr<View> createView(ICoreWebView2Environment *environment,
                                       const std::string &label, int index)
{
    auto view = std::make_unique<View>(); view->label = label;
    // Owned small windows shown without taking foreground activation.
    view->window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"STATIC", L"Feria WebView2 local API probe", WS_OVERLAPPEDWINDOW,
        30 + index * 35, 30 + index * 35, 420, 260, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!view->window) throw std::runtime_error("CreateWindowEx failed");
    ShowWindow(view->window, SW_SHOWNOACTIVATE);
    struct Creation { bool done = false; HRESULT hr = E_PENDING;
                      ComPtr<ICoreWebView2Controller> controller; };
    auto creation = std::make_shared<Creation>();
    check(environment->CreateCoreWebView2Controller(view->window,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [creation](HRESULT hr, ICoreWebView2Controller *controller) -> HRESULT {
                creation->hr = hr; creation->controller = controller;
                creation->done = true; return S_OK;
            }).Get()), "CreateCoreWebView2Controller dispatch");
    waitFor([creation] { return creation->done; }, "controller creation");
    check(creation->hr, "controller creation");
    if (!creation->controller) throw std::runtime_error("missing controller");
    view->controller = creation->controller;
    check(view->controller->get_CoreWebView2(&view->web), "get_CoreWebView2");
    RECT bounds{}; GetClientRect(view->window, &bounds);
    check(view->controller->put_Bounds(bounds), "put_Bounds");
    check(view->controller->put_IsVisible(TRUE), "put_IsVisible");
    ComPtr<ICoreWebView2Settings> settings;
    check(view->web->get_Settings(&settings), "get_Settings");
    check(settings->put_AreDefaultScriptDialogsEnabled(FALSE), "disable default dialogs");
    View *owner = view.get();
    check(view->web->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [owner](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                UINT64 id = 0; LPWSTR uri = nullptr;
                if (!callbackOk(args->get_NavigationId(&id), "start id")) return S_OK;
                if (!callbackOk(args->get_Uri(&uri), "start uri")) return S_OK;
                owner->navigations[id] = Navigation{id, uri ? uri : L""};
                CoTaskMemFree(uri); owner->starts.push_back(id);
                log("navigation-start", owner->label + " id=" + std::to_string(id) +
                    " target=" + utf8(owner->navigations[id].target)); return S_OK;
            }).Get(), &view->starting), "add_NavigationStarting");
    check(view->web->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [owner](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                UINT64 id = 0;
                if (!callbackOk(args->get_NavigationId(&id), "completion id")) return S_OK;
                auto found = owner->navigations.find(id);
                if (found == owner->navigations.end()) {
                    fatal = "completion without captured start"; return S_OK;
                }
                auto &navigation = found->second;
                if (!callbackOk(args->get_IsSuccess(&navigation.success), "completion success")) return S_OK;
                if (!callbackOk(args->get_WebErrorStatus(&navigation.error), "completion error")) return S_OK;
                navigation.completed = true;
                log("navigation-terminal", owner->label + " id=" + std::to_string(id) +
                    " success=" + std::to_string(navigation.success) +
                    " error=" + std::to_string(navigation.error)); return S_OK;
            }).Get(), &view->completed), "add_NavigationCompleted");
    check(view->web->add_ScriptDialogOpening(
        Callback<ICoreWebView2ScriptDialogOpeningEventHandler>(
            [owner](ICoreWebView2 *, ICoreWebView2ScriptDialogOpeningEventArgs *args) -> HRESULT {
                COREWEBVIEW2_SCRIPT_DIALOG_KIND kind{};
                if (!callbackOk(args->get_Kind(&kind), "dialog kind")) return S_OK;
                ++owner->dialogs;
                if (kind != COREWEBVIEW2_SCRIPT_DIALOG_KIND_BEFOREUNLOAD)
                    fatal = "unexpected non-beforeunload dialog";
                LPWSTR uri = nullptr;
                if (!callbackOk(args->get_Uri(&uri), "dialog uri")) return S_OK;
                log("beforeunload", owner->label + " uri=" + utf8(uri ? uri : L"") +
                    (owner->leave ? " choice=Leave" : " choice=Stay"));
                owner->dialogUri = uri ? uri : L"";
                CoTaskMemFree(uri);
                ComPtr<ICoreWebView2Deferral> deferral;
                if (!callbackOk(args->GetDeferral(&deferral), "dialog deferral")) return S_OK;
                ComPtr<ICoreWebView2ScriptDialogOpeningEventArgs> retained = args;
                const bool leave = owner->leave;
                deferred.push_back([retained, deferral, leave, owner] {
                    if (leave) callbackOk(retained->Accept(), "Accept Leave");
                    callbackOk(deferral->Complete(), "dialog Complete");
                    if (leave) ++owner->accepted;
                    else ++owner->denied;
                }); return S_OK;
            }).Get(), &view->dialog), "add_ScriptDialogOpening");
    return view;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 3) { std::cerr << "Usage: probe URL DISPOSABLE_PROFILE [--force-close]\n"; return 2; }
    const bool forceClose = argc > 3 && std::wstring(argv[3]) == L"--force-close";
    std::vector<std::unique_ptr<View>> views;
    ComPtr<ICoreWebView2Environment> environment;
    int result = 1;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(com, "CoInitializeEx");
        struct Creation { bool done = false; HRESULT hr = E_PENDING;
                          ComPtr<ICoreWebView2Environment> environment; };
        auto creation = std::make_shared<Creation>();
        check(CreateCoreWebView2EnvironmentWithOptions(nullptr, argv[2], nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [creation](HRESULT hr, ICoreWebView2Environment *env) -> HRESULT {
                    creation->hr = hr; creation->environment = env;
                    creation->done = true; return S_OK;
                }).Get()), "environment dispatch");
        waitFor([creation] { return creation->done; }, "environment creation");
        check(creation->hr, "environment creation"); environment = creation->environment;
        if (!environment) throw std::runtime_error("missing environment");
        LPWSTR version = nullptr;
        check(environment->get_BrowserVersionString(&version), "runtime version");
        log("runtime", utf8(version ? version : L"")); CoTaskMemFree(version);
        const std::wstring base = argv[1];
        auto addView = [&](const std::string &label) -> View & {
            views.push_back(createView(environment.Get(), label, static_cast<int>(views.size())));
            return *views.back();
        };
        View &sibling = addView("sibling");
        require(sibling.navigate(base + L"?mode=plain").success, "sibling loads");
        const auto siblingState = sibling.script(L"snapshot()");
        const auto siblingEvents = sibling.starts.size();
        View &guarded = addView("guarded");
        require(guarded.navigate(base + L"?mode=guarded").success, "guarded page loads");
        guarded.activate();
        const auto before = guarded.script(L"snapshot()");
        log("snapshot-before", utf8(before));
        BOOL backBefore = FALSE, forwardBefore = FALSE;
        check(guarded.web->get_CanGoBack(&backBefore), "before CanGoBack");
        check(guarded.web->get_CanGoForward(&forwardBefore), "before CanGoForward");
        require(backBefore, "guarded page has native history to preserve");
        if (forceClose) {
            guarded.close();
            negativeDetected = guarded.dialogs == 0;
            require(!negativeDetected, "negative control: forced Close bypasses required consent");
        }
        guarded.leave = false;
        const auto startsBeforeStay = guarded.starts.size();
        const auto sourceBeforeStay = guarded.source();
        check(guarded.web->Navigate(L"about:blank"), "Stay unload dispatch");
        waitFor([&guarded] { return guarded.denied == 1; }, "Stay dialog decision");
        const auto cancelledAt = Clock::now();
        require(guarded.dialogs == 1, "Stay received one native beforeunload dialog");
        require(guarded.dialogUri == sourceBeforeStay,
                "Stay native dialog identifies the exact guarded document URL");
        const auto after = guarded.script(L"snapshot()");
        log("snapshot-after-stay", utf8(after));
        require(before == after, "Stay preserves exact document, form, URL, activation and history length");
        require(guarded.source() == sourceBeforeStay, "Stay preserves native Source as well as DOM URL");
        require(guarded.starts.size() == startsBeforeStay,
                "Stay cancels before NavigationStarting; no terminal navigation ID exists");
        BOOL backAfter = FALSE, forwardAfter = FALSE;
        check(guarded.web->get_CanGoBack(&backAfter), "after CanGoBack");
        check(guarded.web->get_CanGoForward(&forwardAfter), "after CanGoForward");
        require(backBefore == backAfter && forwardBefore == forwardAfter,
                "Stay preserves native back/forward availability");
        guarded.leave = true;
        log("retry-delay-ms", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - cancelledAt).count()));
        const auto left = guarded.navigate(L"about:blank");
        require(left.success && guarded.dialogs == 2 && guarded.accepted == 1 && guarded.denied == 1,
                "same-document retry gets fresh navigation and second beforeunload, then succeeds");
        require(guarded.source() == L"about:blank" &&
                guarded.script(L"typeof documentMarker === 'undefined'") == L"true",
                "Leave actually replaces the guarded document before disposal");
        guarded.close();
        require(sibling.starts.size() == siblingEvents && sibling.script(L"snapshot()") == siblingState,
                "closing intended controller leaves sibling document untouched");
        View &siteNavigation = addView("site-navigation");
        require(siteNavigation.navigate(base + L"?mode=guarded").success,
                "site-navigation guarded page loads");
        siteNavigation.activate();
        const auto siteBefore = siteNavigation.script(L"snapshot()");
        const auto siteSource = siteNavigation.source();
        siteNavigation.leave = false;
        siteNavigation.cdp(L"Input.dispatchMouseEvent", L"{\"type\":\"mousePressed\",\"x\":50,\"y\":160,\"button\":\"left\",\"clickCount\":1}");
        siteNavigation.cdp(L"Input.dispatchMouseEvent", L"{\"type\":\"mouseReleased\",\"x\":50,\"y\":160,\"button\":\"left\",\"clickCount\":1}");
        waitFor([&siteNavigation] { return siteNavigation.denied == 1; }, "site navigation Stay");
        const auto siteCancelledAt = Clock::now();
        require(siteNavigation.script(L"snapshot()") == siteBefore,
                "Stay on trusted site navigation preserves original document and unsaved work");
        require(siteNavigation.source() == siteSource && siteNavigation.dialogUri == siteSource,
                "cancelled site navigation retains native Source and correctly attributed dialog");
        siteNavigation.leave = true;
        log("site-cancel-close-delay-ms", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - siteCancelledAt).count()));
        require(siteNavigation.navigate(L"about:blank").success && siteNavigation.dialogs == 2 &&
                siteNavigation.accepted == 1,
                "immediate close after cancelling site navigation gets fresh consent and completes");
        require(siteNavigation.source() == L"about:blank" &&
                siteNavigation.script(L"typeof documentMarker === 'undefined'") == L"true",
                "close after cancelled site navigation genuinely replaces old document");
        siteNavigation.close();
        require(sibling.starts.size() == siblingEvents && sibling.script(L"snapshot()") == siblingState,
                "sibling also survives close after cancelled site navigation");
        View &plain = addView("no-handler");
        require(plain.navigate(base + L"?mode=plain").success, "no-handler page loads");
        require(plain.navigate(L"about:blank").success && plain.dialogs == 0,
                "no-handler unload succeeds without synthetic consent dialog");
        plain.close();
        View &noGesture = addView("no-gesture");
        require(noGesture.navigate(base + L"?mode=guarded").success, "unactivated guarded page loads");
        require(noGesture.cdp(L"Runtime.evaluate", L"{\"expression\":\"navigator.userActivation.hasBeenActive\",\"returnByValue\":true,\"userGesture\":false}") ==
                L"{\"result\":{\"type\":\"boolean\",\"value\":false}}",
                "no-gesture fixture really lacks activation");
        require(noGesture.navigate(L"about:blank").success && noGesture.dialogs == 0,
                "browser suppresses unactivated unload dialog, terminal departure still required");
        noGesture.close();
        View &forced = addView("raw-close");
        require(forced.navigate(base + L"?mode=guarded").success, "raw-close guarded page loads");
        forced.activate(); forced.close();
        require(forced.dialogs == 0, "raw controller Close bypasses activated beforeunload");
        result = 0;
    } catch (const std::exception &error) { log("error", error.what()); }
    for (auto &view : views) {
        if (view->controller) view->controller->Close();
        view->web.Reset(); view->controller.Reset();
        if (view->window) DestroyWindow(view->window);
    }
    environment.Reset();
    if (SUCCEEDED(com)) CoUninitialize();
    std::cout << "{\"event\":\"summary\",\"passed\":" << passed
              << ",\"failed\":" << (result ? 1 : 0)
              << ",\"negativeDetected\":" << (negativeDetected ? "true" : "false")
              << "}" << std::endl;
    return result;
}
