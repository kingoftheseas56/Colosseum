#pragma once

// MpvNativeVideoWindow — Harbor-style native video surface (Player 1.5 wid spike, 2026-09-19).
//
// A frameless, never-activated top-level Win32 window that mpv paints into directly via the
// `wid` property (vo=gpu-next, its own D3D11 swapchain). DWM composites it UNDER the app's
// translucent Qt window, so video reaches the screen without ever passing through mpvqt's
// OpenGL FBO and the Qt scene graph — the measured ~30-35ms/frame presentation margin of the
// render-API path. Geometry mirrors the app window's native rect; z-order is pinned directly
// beneath it, so all QML chrome (seek bar, menus, pause card) stays above the video and keeps
// every input event (mpv input bindings are already disabled app-wide).
//
// Verified against Harbor's own Windows embedding (src-tauri/src/mpv.rs: set_property("wid", ...)).
// Empty on non-Windows builds; the spike is Windows-only by design (see
// docs/superpowers/plans/2026-09-19-player15-wid-spike.md, untracked).

#include <QtGlobal>

#ifdef Q_OS_WIN

using MpvNativeWindowHandle = void *;   // HWND without dragging windows.h into every includer

class MpvNativeVideoWindow
{
public:
    MpvNativeVideoWindow() = default;
    ~MpvNativeVideoWindow();

    MpvNativeVideoWindow(const MpvNativeVideoWindow &) = delete;
    MpvNativeVideoWindow &operator=(const MpvNativeVideoWindow &) = delete;

    bool create();
    void destroy();
    bool isValid() const { return m_hwnd != nullptr; }

    // The `wid` value mpv receives.
    qintptr wid() const { return reinterpret_cast<qintptr>(m_hwnd); }

    // Match the app window's current native rect exactly and place the video window directly
    // beneath it in z-order (main stays above, receives focus and input). No-op if invalid.
    void placeUnder(MpvNativeWindowHandle mainWindow);

    void hide();

private:
    static bool ensureWindowClass();
    // WNDPROC-shaped without naming WINAPI in this header (windows.h stays in the .cpp;
    // on x64 there is a single calling convention, and the .cpp casts to WNDPROC).
    static long long wndProc(void *hwnd, unsigned int message,
                             unsigned long long wParam, long long lParam);

    void *m_hwnd = nullptr;   // HWND
};

#endif // Q_OS_WIN
