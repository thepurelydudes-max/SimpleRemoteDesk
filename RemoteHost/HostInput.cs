using System.Runtime.InteropServices;

namespace RemoteHost;

internal static class HostInput
{
    private const uint INPUT_MOUSE = 0;
    private const uint INPUT_KEYBOARD = 1;

    private const uint MOUSEEVENTF_LEFTDOWN = 0x0002;
    private const uint MOUSEEVENTF_LEFTUP = 0x0004;
    private const uint MOUSEEVENTF_RIGHTDOWN = 0x0008;
    private const uint MOUSEEVENTF_RIGHTUP = 0x0010;
    private const uint MOUSEEVENTF_MIDDLEDOWN = 0x0020;
    private const uint MOUSEEVENTF_MIDDLEUP = 0x0040;
    private const uint MOUSEEVENTF_WHEEL = 0x0800;

    private const uint KEYEVENTF_EXTENDEDKEY = 0x0001;
    private const uint KEYEVENTF_KEYUP = 0x0002;

    // Build-Windows.bat публикует только win-x64. В native INPUT на x64
    // union начинается со смещения 8, а полный размер структуры равен 40 байтам.
    [StructLayout(LayoutKind.Explicit, Size = 40)]
    private struct INPUT
    {
        [FieldOffset(0)] public uint type;
        [FieldOffset(8)] public InputUnion U;
    }

    [StructLayout(LayoutKind.Explicit)]
    private struct InputUnion
    {
        [FieldOffset(0)] public MOUSEINPUT mi;
        [FieldOffset(0)] public KEYBDINPUT ki;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MOUSEINPUT
    {
        public int dx;
        public int dy;
        public uint mouseData;
        public uint dwFlags;
        public uint time;
        public IntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct KEYBDINPUT
    {
        public ushort wVk;
        public ushort wScan;
        public uint dwFlags;
        public uint time;
        public IntPtr dwExtraInfo;
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool SetCursorPos(int X, int Y);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);

    [DllImport("user32.dll")]
    private static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, UIntPtr dwExtraInfo);

    [DllImport("user32.dll")]
    private static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);

    public static void MoveMouse(int x, int y)
    {
        Rectangle bounds = Screen.PrimaryScreen?.Bounds ?? Rectangle.Empty;
        if (bounds == Rectangle.Empty) return;
        x = Math.Clamp(x, 0, bounds.Width - 1) + bounds.Left;
        y = Math.Clamp(y, 0, bounds.Height - 1) + bounds.Top;
        if (!SetCursorPos(x, y))
        {
            int error = Marshal.GetLastWin32Error();
            AppLog.Write($"SetCursorPos failed. Win32={error}");
        }
    }

    public static void MouseButton(byte button, bool down)
    {
        uint flags = button switch
        {
            1 => down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP,
            2 => down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP,
            3 => down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP,
            _ => 0
        };
        if (flags == 0) return;

        uint sent = SendInputChecked(new[]
        {
            new INPUT
            {
                type = INPUT_MOUSE,
                U = new InputUnion { mi = new MOUSEINPUT { dwFlags = flags } }
            }
        });
        if (sent == 0)
            mouse_event(flags, 0, 0, 0, UIntPtr.Zero);
    }

    public static void MouseWheel(int delta)
    {
        uint sent = SendInputChecked(new[]
        {
            new INPUT
            {
                type = INPUT_MOUSE,
                U = new InputUnion
                {
                    mi = new MOUSEINPUT
                    {
                        mouseData = unchecked((uint)delta),
                        dwFlags = MOUSEEVENTF_WHEEL
                    }
                }
            }
        });
        if (sent == 0)
            mouse_event(MOUSEEVENTF_WHEEL, 0, 0, unchecked((uint)delta), UIntPtr.Zero);
    }

    public static void Key(int virtualKey, bool down)
    {
        uint sent = SendInputChecked(new[] { BuildKeyInput(virtualKey, down) });
        if (sent == 0)
        {
            uint flags = down ? 0u : KEYEVENTF_KEYUP;
            if (IsExtendedKey(virtualKey)) flags |= KEYEVENTF_EXTENDEDKEY;
            keybd_event(unchecked((byte)virtualKey), 0, flags, UIntPtr.Zero);
        }
    }

    public static void KeyCombination(IReadOnlyList<int> virtualKeys)
    {
        if (virtualKeys.Count == 0 || virtualKeys.Count > 16) return;
        var inputs = new INPUT[virtualKeys.Count * 2];
        int index = 0;
        for (int i = 0; i < virtualKeys.Count; i++)
            inputs[index++] = BuildKeyInput(virtualKeys[i], true);
        for (int i = virtualKeys.Count - 1; i >= 0; i--)
            inputs[index++] = BuildKeyInput(virtualKeys[i], false);
        uint sent = SendInputChecked(inputs);
        if (sent == 0)
        {
            for (int i = 0; i < virtualKeys.Count; i++)
                LegacyKey(virtualKeys[i], true);
            for (int i = virtualKeys.Count - 1; i >= 0; i--)
                LegacyKey(virtualKeys[i], false);
        }
    }

    private static INPUT BuildKeyInput(int virtualKey, bool down)
    {
        uint flags = down ? 0u : KEYEVENTF_KEYUP;
        if (IsExtendedKey(virtualKey)) flags |= KEYEVENTF_EXTENDEDKEY;
        return new INPUT
        {
            type = INPUT_KEYBOARD,
            U = new InputUnion
            {
                ki = new KEYBDINPUT
                {
                    wVk = unchecked((ushort)virtualKey),
                    dwFlags = flags
                }
            }
        };
    }

    private static uint SendInputChecked(INPUT[] inputs)
    {
        if (inputs.Length == 0) return 0;
        uint sent = SendInput((uint)inputs.Length, inputs, Marshal.SizeOf<INPUT>());
        if (sent == 0)
        {
            int error = Marshal.GetLastWin32Error();
            AppLog.Write($"SendInput returned 0. Win32={error}, INPUT size={Marshal.SizeOf<INPUT>()}");
        }
        return sent;
    }

    private static void LegacyKey(int virtualKey, bool down)
    {
        uint flags = down ? 0u : KEYEVENTF_KEYUP;
        if (IsExtendedKey(virtualKey)) flags |= KEYEVENTF_EXTENDEDKEY;
        keybd_event(unchecked((byte)virtualKey), 0, flags, UIntPtr.Zero);
    }

    private static bool IsExtendedKey(int vk) => vk is
        0x21 or 0x22 or 0x23 or 0x24 or 0x25 or 0x26 or 0x27 or 0x28 or
        0x2D or 0x2E or 0x5B or 0x5C or 0x6F or 0x90 or 0xA3 or 0xA5;
}
