param(
    [Parameter(Mandatory=$true)][string]$ExePath,
    [Parameter(Mandatory=$true)][string]$ManagedRelativePath
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ExePath)) {
    throw "AppHost not found: $ExePath"
}

$bytes = [System.IO.File]::ReadAllBytes($ExePath)
$old = [System.Text.Encoding]::UTF8.GetBytes('SimpleRemoteDesk.dll')
$new = [System.Text.Encoding]::UTF8.GetBytes($ManagedRelativePath)

if ($new.Length -ge 1024) {
    throw "Managed path is too long for .NET apphost: $ManagedRelativePath"
}

function Find-AppHostPathSlot([byte[]]$Data, [byte[]]$Needle, [int]$RequiredLength) {
    for ($i = 0; $i -le $Data.Length - $Needle.Length; $i++) {
        $match = $true
        for ($j = 0; $j -lt $Needle.Length; $j++) {
            if ($Data[$i + $j] -ne $Needle[$j]) { $match = $false; break }
        }
        if (-not $match) { continue }

        # The .NET apphost keeps the managed DLL path in a fixed 1024-byte slot.
        # After the current null-terminated path there should be zero padding.
        if ($i + $RequiredLength + 1 -gt $Data.Length) { continue }
        $paddingOk = $true
        for ($k = $Needle.Length; $k -le $RequiredLength; $k++) {
            if ($Data[$i + $k] -ne 0) { $paddingOk = $false; break }
        }
        if ($paddingOk) { return $i }
    }
    return -1
}

$index = Find-AppHostPathSlot $bytes $old $new.Length
if ($index -lt 0) {
    throw "Could not locate the apphost managed-path slot in $ExePath"
}

# Clear enough of the slot for the new path, then write the relative managed DLL path.
for ($i = 0; $i -le $new.Length; $i++) { $bytes[$index + $i] = 0 }
[Array]::Copy($new, 0, $bytes, $index, $new.Length)
$bytes[$index + $new.Length] = 0

[System.IO.File]::WriteAllBytes($ExePath, $bytes)
Write-Host "AppHost patched: $ManagedRelativePath"
