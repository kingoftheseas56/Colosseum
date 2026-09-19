#include "MpvNativeVideoWindow.h"

#ifdef Q_OS_WIN

#include <windows.h>

namespace {
constexpr wchar_t kClassName[] = L"ColosseumMpvNativeVideo";
}

bool MpvNativeVideoWindow::ensureWindowClass()
{
    static bool registered = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&MpvNativeVideoWindow::wndProc);
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        return RegisterClassExW(&wc) != 0;
    }();
    return registered;
}

long long MpvNativeVideoWindow::wndProc(void *hwnd, unsigned int message,
                                        unsigned long long wParam, long long lParam)
{
    // The window exists to be painted by mpv; every interaction stays in the Qt window above.
    if (message == WM_NCHITTEST)
        return HTTRANSPARENT;
    return DefWindowProcW(static_cast<HWND>(hwnd), message,
                          static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam));
}

bool MpvNativeVideoWindow::create()
{
    if (m_hwnd)
        return true;
    if (!ensureWindowClass())
        return false;
    // Hidden until the first placeUnder(): mpv may attach before the player window has a
    // meaningful rect. WS_EX_NOACTIVATE keeps it from ever taking focus from the UI.
    m_hwnd = CreateWindowExW(WS_EX_NOACTIVATE,
                             kClassName, L"Colosseum video",
                             WS_POPUP,
                             0, 0, 1, 1,
                             nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    return m_hwnd != nullptr;
}

void MpvNativeVideoWindow::destroy()
{
    if (!m_hwnd)
        return;
    DestroyWindow(static_cast<HWND>(m_hwnd));
    m_hwnd = nullptr;
}

MpvNativeVideoWindow::~MpvNativeVideoWindow()
{
    destroy();
}

void MpvNativeVideoWindow::placeUnder(MpvNativeWindowHandle mainWindow)
{
    if (!m_hwnd || !mainWindow)
        return;
    const HWND main = static_cast<HWND>(mainWindow);
    RECT r{};
    if (!GetWindowRect(main, &r))
        return;
    // hwndInsertAfter = main: this window lands directly BELOW the app window in z-order.
    SetWindowPos(static_cast<HWND>(m_hwnd), main,
                 r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void MpvNativeVideoWindow::hide()
{
    if (m_hwnd)
        ShowWindow(static_cast<HWND>(m_hwnd), SW_HIDE);
}

#endif // Q_OS_WIN
