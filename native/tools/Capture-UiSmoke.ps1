param(
    [string]$BuildDir = "build/native/src/Release",
    [string]$OutDir = "ui-smoke"
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;

public static class Win32Capture {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
}
"@

function Wait-MainWindow {
    param(
        [System.Diagnostics.Process]$Process,
        [int]$TimeoutMs = 10000
    )

    $sw = [System.Diagnostics.Stopwatch]::StartNew()

    while ($sw.ElapsedMilliseconds -lt $TimeoutMs) {
        $Process.Refresh()

        if ($Process.MainWindowHandle -ne [IntPtr]::Zero) {
            return $Process.MainWindowHandle
        }

        Start-Sleep -Milliseconds 150
    }

    throw "No main window for process $($Process.ProcessName)"
}

function Capture-Window {
    param(
        [IntPtr]$Handle,
        [string]$Path
    )

    [Win32Capture]::ShowWindow($Handle, 5) | Out-Null
    [Win32Capture]::SetForegroundWindow($Handle) | Out-Null
    Start-Sleep -Milliseconds 400

    $rect = New-Object Win32Capture+RECT

    if (-not [Win32Capture]::GetWindowRect($Handle, [ref]$rect)) {
        throw "GetWindowRect failed"
    }

    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top

    if ($width -lt 100 -or $height -lt 100) {
        throw "Window size is invalid: $width x $height"
    }

    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()

    try {
        if (-not [Win32Capture]::PrintWindow($Handle, $hdc, 2)) {
            throw "PrintWindow failed"
        }
    }
    finally {
        $graphics.ReleaseHdc($hdc)
        $graphics.Dispose()
    }

    $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
}

$host = $null
$viewer = $null

try {
    $host = Start-Process -FilePath (Join-Path $BuildDir "SimpleRemoteHostGui.exe") -PassThru
    $hostHandle = Wait-MainWindow -Process $host
    Capture-Window -Handle $hostHandle -Path (Join-Path $OutDir "host.png")

    $viewer = Start-Process -FilePath (Join-Path $BuildDir "SimpleRemoteViewerGui.exe") -PassThru
    $viewerHandle = Wait-MainWindow -Process $viewer
    Capture-Window -Handle $viewerHandle -Path (Join-Path $OutDir "viewer.png")
}
finally {
    if ($viewer -and -not $viewer.HasExited) {
        Stop-Process -Id $viewer.Id -Force -ErrorAction SilentlyContinue
    }

    if ($host -and -not $host.HasExited) {
        Stop-Process -Id $host.Id -Force -ErrorAction SilentlyContinue
    }
}
