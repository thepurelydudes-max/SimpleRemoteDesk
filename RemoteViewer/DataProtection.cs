using System.Security.Cryptography;
using System.Text;

namespace RemoteViewer;

internal static class DataProtection
{
    private static readonly byte[] Entropy = Encoding.UTF8.GetBytes("SimpleRemoteDesk-v2");

    public static string Protect(string value)
    {
        if (string.IsNullOrEmpty(value)) return string.Empty;
        byte[] data = Encoding.UTF8.GetBytes(value);
        byte[] encrypted = ProtectedData.Protect(data, Entropy, DataProtectionScope.CurrentUser);
        CryptographicOperations.ZeroMemory(data);
        return Convert.ToBase64String(encrypted);
    }

    public static string Unprotect(string value)
    {
        if (string.IsNullOrWhiteSpace(value)) return string.Empty;
        try
        {
            byte[] encrypted = Convert.FromBase64String(value);
            byte[] data = ProtectedData.Unprotect(encrypted, Entropy, DataProtectionScope.CurrentUser);
            string result = Encoding.UTF8.GetString(data);
            CryptographicOperations.ZeroMemory(data);
            return result;
        }
        catch
        {
            return string.Empty;
        }
    }
}
