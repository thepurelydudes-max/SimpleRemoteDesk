using Microsoft.Win32;

namespace RemoteHost;

internal static class StartupManager
{
    private const string RunKeyPath = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueName = "SimpleRemoteDeskHost";

    public static void SetEnabled(bool enabled)
    {
        using RegistryKey key = Registry.CurrentUser.CreateSubKey(RunKeyPath);
        if (enabled)
        {
            string exe = Application.ExecutablePath;
            key.SetValue(ValueName, $"\"{exe}\" --autostart");
        }
        else
        {
            key.DeleteValue(ValueName, false);
        }
    }
}
