# wheel_latency_drive — TEMPORARY diagnostic (Agent 1, 2026-07-17). Injects wheel notches at
# known epoch-ms timestamps into the harness window so the QML log's WHEEL/FRAME stamps can be
# correlated against injection time. House doctrine: DPI-aware, foreground check BEFORE input.
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WheelDrv {
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, int data, UIntPtr extra);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    public const uint MOUSEEVENTF_WHEEL = 0x0800;
}
"@
[void][System.Runtime.InteropServices.Marshal]::GetLastWin32Error()

$hwnd = [IntPtr]::Zero
foreach ($i in 1..40) {
    $p = Get-Process qml -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if ($p) { $hwnd = $p.MainWindowHandle; break }
    Start-Sleep -Milliseconds 250
}
if ($hwnd -eq [IntPtr]::Zero) { Write-Output "DRV FAIL window-not-found"; exit 1 }

[void][WheelDrv]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 400
$r = New-Object WheelDrv+RECT
[void][WheelDrv]::GetWindowRect($hwnd, [ref]$r)
$cx = [int](($r.L + $r.R) / 2); $cy = [int](($r.T + $r.B) / 2)
[void][WheelDrv]::SetCursorPos($cx, $cy)
Start-Sleep -Milliseconds 400
# Win10/11 routes wheel to the window under the CURSOR ("scroll inactive windows" default-on),
# so cursor-over-window is the real requirement; foreground is best-effort.
$fg = if ([WheelDrv]::GetForegroundWindow() -eq $hwnd) { "foreground" } else { "background(cursor-routed)" }
Write-Output ("DRV READY center=" + $cx + "," + $cy + " " + $fg)

# 6 single notches from idle (800ms apart = each starts from a cold render loop),
# then a 4-notch flurry (idle-to-burst).
foreach ($i in 1..6) {
    Start-Sleep -Milliseconds 800
    $t = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    [WheelDrv]::mouse_event([WheelDrv]::MOUSEEVENTF_WHEEL, 0, 0, -120, [UIntPtr]::Zero)
    Write-Output ("SEND " + $t)
}
Start-Sleep -Milliseconds 1200
foreach ($i in 1..4) {
    $t = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    [WheelDrv]::mouse_event([WheelDrv]::MOUSEEVENTF_WHEEL, 0, 0, -120, [UIntPtr]::Zero)
    Write-Output ("SEND " + $t)
    Start-Sleep -Milliseconds 60
}
Start-Sleep -Milliseconds 800
Write-Output "DRV DONE"
