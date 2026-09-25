using System.Runtime.InteropServices;

namespace RemoteHost;

internal enum RemoteCursorKind : byte
{
    Arrow = 0,
    IBeam = 1,
    Wait = 2,
    Cross = 3,
    UpArrow = 4,
    SizeNWSE = 5,
    SizeNESW = 6,
    SizeWE = 7,
    SizeNS = 8,
    SizeAll = 9,
    No = 10,
    Hand = 11,
    AppStarting = 12,
    Help = 13,
    Unknown = 255
}

internal static class CursorState
{
    private const int CURSOR_SHOWING = 0x00000001;

    private const int IDC_ARROW = 32512;
    private const int IDC_IBEAM = 32513;
    private const int IDC_WAIT = 32514;
    private const int IDC_CROSS = 32515;
    private const int IDC_UPARROW = 32516;
    private const int IDC_SIZENWSE = 32642;
    private const int IDC_SIZENESW = 32643;
    private const int IDC_SIZEWE = 32644;
    private const int IDC_SIZENS = 32645;
    private const int IDC_SIZEALL = 32646;
    private const int IDC_NO = 32648;
    private const int IDC_HAND = 32649;
    private const int IDC_APPSTARTING = 32650;
    private const int IDC_HELP = 32651;

    [StructLayout(LayoutKind.Sequential)]
    private struct CURSORINFO
    {
        public int cbSize;
        public int flags;
        public IntPtr hCursor;
        public POINT ptScreenPos;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct POINT
    {
        public int X;
        public int Y;
    }

    [DllImport("user32.dll")]
    private static extern bool GetCursorInfo(ref CURSORINFO pci);

    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    private static extern IntPtr LoadCursor(IntPtr hInstance, IntPtr lpCursorName);

    private static readonly Dictionary<IntPtr, RemoteCursorKind> Known = BuildKnown();

    public static RemoteCursorKind GetCurrentKind()
    {
        var ci = new CURSORINFO { cbSize = Marshal.SizeOf<CURSORINFO>() };
        if (!GetCursorInfo(ref ci) || (ci.flags & CURSOR_SHOWING) == 0 || ci.hCursor == IntPtr.Zero)
            return RemoteCursorKind.Arrow;

        return Known.TryGetValue(ci.hCursor, out RemoteCursorKind kind) ? kind : RemoteCursorKind.Arrow;
    }

    private static Dictionary<IntPtr, RemoteCursorKind> BuildKnown()
    {
        var map = new Dictionary<IntPtr, RemoteCursorKind>();
        Add(map, IDC_ARROW, RemoteCursorKind.Arrow);
        Add(map, IDC_IBEAM, RemoteCursorKind.IBeam);
        Add(map, IDC_WAIT, RemoteCursorKind.Wait);
        Add(map, IDC_CROSS, RemoteCursorKind.Cross);
        Add(map, IDC_UPARROW, RemoteCursorKind.UpArrow);
        Add(map, IDC_SIZENWSE, RemoteCursorKind.SizeNWSE);
        Add(map, IDC_SIZENESW, RemoteCursorKind.SizeNESW);
        Add(map, IDC_SIZEWE, RemoteCursorKind.SizeWE);
        Add(map, IDC_SIZENS, RemoteCursorKind.SizeNS);
        Add(map, IDC_SIZEALL, RemoteCursorKind.SizeAll);
        Add(map, IDC_NO, RemoteCursorKind.No);
        Add(map, IDC_HAND, RemoteCursorKind.Hand);
        Add(map, IDC_APPSTARTING, RemoteCursorKind.AppStarting);
        Add(map, IDC_HELP, RemoteCursorKind.Help);
        return map;
    }

    private static void Add(Dictionary<IntPtr, RemoteCursorKind> map, int resourceId, RemoteCursorKind kind)
    {
        IntPtr handle = LoadCursor(IntPtr.Zero, new IntPtr(resourceId));
        if (handle != IntPtr.Zero) map[handle] = kind;
    }
}
