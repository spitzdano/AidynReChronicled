# add-to-steam.ps1: add Aidyn ReChronicled to Steam as a non-Steam game, with its library art. The Windows download's
# "Add to Steam.bat" runs it (Windows PowerShell 5.1 or PowerShell 7); it does what tools/ally/steam_shortcut.py does
# on Linux, the same way:
#   1. reads userdata\<account>\config\shortcuts.vdf (Steam's binary key-value format) for every Steam account on the
#      PC. It writes nothing unless reading and re-writing the file gives back exactly the same bytes, so a format it
#      doesn't understand is never damaged. A copy is kept as shortcuts.vdf.before-aidyn.
#   2. adds the shortcut, or updates the one it added before (found by its Exe: AidynReChronicled.exe): name
#      "Aidyn ReChronicled", the exe, its folder, the icon (steam-art\icon.png), no launch options.
#   3. copies the art in steam-art\ into userdata\<account>\config\grid, named by the shortcut's app id the way Steam
#      looks for it: <id>p.png (library capsule), <id>.png (wide capsule), <id>_hero.png, <id>_logo.png.
# Steam must be closed while it writes: it offers to close Steam and starts it again afterwards.
#
#   add-to-steam.ps1 [-DryRun] [-GameDir <folder>] [-SteamPath <folder>]   (the last two for tests)
# Plain ASCII on purpose: Windows PowerShell 5.1 reads a file without a byte-order mark in the system's code page.
param(
    [string]$GameDir = $PSScriptRoot,
    [string]$SteamPath = "",
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$Name = 'Aidyn ReChronicled'
# Any stop: one plain line for the player, not PowerShell's report.
trap {
    Write-Host ''
    Write-Host "Not added to Steam: $($_.Exception.Message)" -ForegroundColor Yellow
    exit 1
}

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

// Binary VDF: a type byte, a NUL-terminated key, then the value. 0x00 map (until 0x08), 0x01 string, 0x02 int32,
// 0x03 float32, 0x07 uint64, 0x0A int64.
public class AidynVdfNode {
    public byte Type;
    public byte[] Key;
    public byte[] Raw;                  // a string's bytes (without the NUL) or a number's
    public List<AidynVdfNode> Children; // a map's
}

public static class AidynVdf {
    public static List<AidynVdfNode> Parse(byte[] d) {
        int p = 0;
        List<AidynVdfNode> items = Items(d, ref p);
        if (p != d.Length) throw new Exception("data after the end");
        return items;
    }

    static byte[] CStr(byte[] d, ref int p) {
        int e = Array.IndexOf(d, (byte)0, p);
        if (e < 0) throw new Exception("unterminated string");
        byte[] s = new byte[e - p];
        Array.Copy(d, p, s, 0, e - p);
        p = e + 1;
        return s;
    }

    static List<AidynVdfNode> Items(byte[] d, ref int p) {
        List<AidynVdfNode> o = new List<AidynVdfNode>();
        while (true) {
            if (p >= d.Length) throw new Exception("truncated");
            byte t = d[p++];
            if (t == 8) return o;
            AidynVdfNode n = new AidynVdfNode();
            n.Type = t;
            n.Key = CStr(d, ref p);
            if (t == 0) {
                n.Children = Items(d, ref p);
            } else if (t == 1) {
                n.Raw = CStr(d, ref p);
            } else {
                int len = (t == 2 || t == 3) ? 4 : (t == 7 || t == 10) ? 8 : -1;
                if (len < 0) throw new Exception("unknown type " + t);
                if (p + len > d.Length) throw new Exception("truncated");
                n.Raw = new byte[len];
                Array.Copy(d, p, n.Raw, 0, len);
                p += len;
            }
            o.Add(n);
        }
    }

    public static byte[] Dump(List<AidynVdfNode> items) {
        MemoryStream ms = new MemoryStream();
        Write(ms, items);
        return ms.ToArray();
    }

    static void Write(MemoryStream ms, List<AidynVdfNode> items) {
        foreach (AidynVdfNode n in items) {
            ms.WriteByte(n.Type);
            ms.Write(n.Key, 0, n.Key.Length);
            ms.WriteByte(0);
            if (n.Type == 0) {
                Write(ms, n.Children);
            } else if (n.Type == 1) {
                ms.Write(n.Raw, 0, n.Raw.Length);
                ms.WriteByte(0);
            } else {
                ms.Write(n.Raw, 0, n.Raw.Length);
            }
        }
        ms.WriteByte(8);
    }

    public static AidynVdfNode Get(List<AidynVdfNode> items, string key) {
        byte[] k = Encoding.UTF8.GetBytes(key);
        foreach (AidynVdfNode n in items) {
            if (n.Key.Length == k.Length && Encoding.UTF8.GetString(n.Key) == key) return n;
        }
        return null;
    }

    public static AidynVdfNode Str(string key, string value) {
        AidynVdfNode n = new AidynVdfNode();
        n.Type = 1; n.Key = Encoding.UTF8.GetBytes(key); n.Raw = Encoding.UTF8.GetBytes(value);
        return n;
    }

    public static AidynVdfNode Int(string key, int value) {
        AidynVdfNode n = new AidynVdfNode();
        n.Type = 2; n.Key = Encoding.UTF8.GetBytes(key); n.Raw = BitConverter.GetBytes(value);
        return n;
    }

    public static AidynVdfNode Map(string key, List<AidynVdfNode> children) {
        AidynVdfNode n = new AidynVdfNode();
        n.Type = 0; n.Key = Encoding.UTF8.GetBytes(key); n.Children = children;
        return n;
    }

    public static bool Contains(byte[] hay, byte[] needle) {
        for (int i = 0; i + needle.Length <= hay.Length; i++) {
            int j = 0;
            while (j < needle.Length && hay[i + j] == needle[j]) j++;
            if (j == needle.Length) return true;
        }
        return false;
    }

    // Steam's id for a shortcut: CRC-32 of the quoted exe and the name, top bit set.
    public static uint AppId(string exeQuoted, string name) {
        byte[] b = Encoding.UTF8.GetBytes(exeQuoted + name);
        uint crc = 0xFFFFFFFF;
        foreach (byte x in b) {
            crc ^= x;
            for (int k = 0; k < 8; k++) crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
        return ~crc | 0x80000000u;
    }
}
'@

function Say($text) { Write-Host $text }

$onWindows = ($PSVersionTable.PSVersion.Major -lt 6) -or $IsWindows
$exe = Join-Path $GameDir 'AidynReChronicled.exe'
$art = Join-Path $GameDir 'steam-art'
$icon = Join-Path $art 'icon.png'
if (-not (Test-Path -LiteralPath $exe)) { throw "AidynReChronicled.exe isn't next to this script ($GameDir)." }

$steamExe = $null
if ($SteamPath -eq '') {
    $reg = Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue
    if ($reg -and $reg.SteamPath) { $SteamPath = $reg.SteamPath -replace '/', '\' }
    if ($reg -and $reg.SteamExe) { $steamExe = $reg.SteamExe -replace '/', '\' }
    if ($SteamPath -eq '' -and (Test-Path "${env:ProgramFiles(x86)}\Steam")) { $SteamPath = "${env:ProgramFiles(x86)}\Steam" }
}
if ($SteamPath -eq '' -or -not (Test-Path -LiteralPath (Join-Path $SteamPath 'userdata'))) {
    throw "Steam wasn't found. Start Steam once and log in, then run this again."
}
if (-not $steamExe) { $steamExe = Join-Path $SteamPath 'steam.exe' }
$accounts = @(Get-ChildItem -LiteralPath (Join-Path $SteamPath 'userdata') -Directory | Where-Object { $_.Name -match '^\d+$' -and $_.Name -ne '0' })
if ($accounts.Count -eq 0) { throw "No Steam account in $SteamPath\userdata: log in to Steam once first." }

# Steam keeps shortcuts.vdf open and writes it when it closes: it has to be closed first.
$closedSteam = $false
if ($onWindows -and -not $DryRun -and (Get-Process -Name 'steam' -ErrorAction SilentlyContinue)) {
    $answer = Read-Host "Steam is running and has to close for a moment. Close it now? [Y/n]"
    if ($answer -match '^[nN]') { Say 'Nothing changed. Close Steam and run this again.'; return }
    Start-Process -FilePath $steamExe -ArgumentList '-shutdown'
    for ($i = 0; $i -lt 60 -and (Get-Process -Name 'steam' -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Seconds 1 }
    if (Get-Process -Name 'steam' -ErrorAction SilentlyContinue) { throw "Steam didn't close. Close it (Steam menu > Exit) and run this again." }
    Start-Sleep -Seconds 2
    $closedSteam = $true
}

$exeQuoted = '"' + $exe + '"'
$appId = [AidynVdf]::AppId($exeQuoted, $Name)
$exeName = [Text.Encoding]::UTF8.GetBytes('AidynReChronicled.exe')
foreach ($account in $accounts) {
    $cfg = Join-Path $account.FullName 'config'
    $vdf = Join-Path $cfg 'shortcuts.vdf'
    $data = $null
    if (Test-Path -LiteralPath $vdf) {
        $data = [IO.File]::ReadAllBytes($vdf)
        try { $tree = [AidynVdf]::Parse($data) } catch { throw "Can't read $vdf ($($_.Exception.Message)); nothing changed. Add the game by hand (the README, 'Adding it to Steam')." }
        $again = [AidynVdf]::Dump($tree)
        if ([Convert]::ToBase64String($again) -ne [Convert]::ToBase64String($data)) {
            throw "$vdf doesn't read back the same; nothing changed. Add the game by hand (the README, 'Adding it to Steam')."
        }
    } else {
        $tree = New-Object 'System.Collections.Generic.List[AidynVdfNode]'
        $tree.Add([AidynVdf]::Map('shortcuts', (New-Object 'System.Collections.Generic.List[AidynVdfNode]')))
    }
    $shortcuts = [AidynVdf]::Get($tree, 'shortcuts')
    if ($null -eq $shortcuts -or $shortcuts.Type -ne 0) { throw "$vdf has no shortcuts list; nothing changed." }
    $entries = $shortcuts.Children
    $mine = $null
    foreach ($e in $entries) {
        $ex = [AidynVdf]::Get($e.Children, 'Exe')
        if ($null -eq $ex) { $ex = [AidynVdf]::Get($e.Children, 'exe') }
        if ($ex -and [AidynVdf]::Contains($ex.Raw, $exeName)) { $mine = $e; break }
    }
    $id = $appId
    if ($mine) {
        $old = [AidynVdf]::Get($mine.Children, 'appid')
        if ($old) { $id = [BitConverter]::ToUInt32($old.Raw, 0) }  # keep the id Steam knows (play time, art)
    }
    $fields = New-Object 'System.Collections.Generic.List[AidynVdfNode]'
    $fields.Add([AidynVdf]::Int('appid', [BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]$id), 0)))
    $fields.Add([AidynVdf]::Str('AppName', $Name))
    $fields.Add([AidynVdf]::Str('Exe', $exeQuoted))
    $fields.Add([AidynVdf]::Str('StartDir', '"' + $GameDir + '"'))
    $fields.Add([AidynVdf]::Str('icon', $icon))
    foreach ($s in 'ShortcutPath', 'LaunchOptions') { $fields.Add([AidynVdf]::Str($s, '')) }
    foreach ($k in @(@('IsHidden', 0), @('AllowDesktopConfig', 1), @('AllowOverlay', 1), @('OpenVR', 0), @('Devkit', 0))) {
        $fields.Add([AidynVdf]::Int($k[0], $k[1]))
    }
    $fields.Add([AidynVdf]::Str('DevkitGameID', ''))
    $fields.Add([AidynVdf]::Int('DevkitOverrideAppID', 0))
    $fields.Add([AidynVdf]::Int('LastPlayTime', 0))
    $fields.Add([AidynVdf]::Str('FlatpakAppID', ''))
    $fields.Add([AidynVdf]::Str('sortas', ''))
    $fields.Add([AidynVdf]::Map('tags', (New-Object 'System.Collections.Generic.List[AidynVdfNode]')))
    if ($mine) {
        $mine.Children = $fields
        $action = 'updated'
    } else {
        $entries.Add([AidynVdf]::Map([string]$entries.Count, $fields))
        $action = 'added'
    }
    Say "$($account.Name): shortcut $action (app id $id), art -> $(Join-Path $cfg 'grid')"
    if ($DryRun) { continue }
    if ($null -ne $data -and -not (Test-Path -LiteralPath (Join-Path $cfg 'shortcuts.vdf.before-aidyn'))) {
        Copy-Item -LiteralPath $vdf -Destination (Join-Path $cfg 'shortcuts.vdf.before-aidyn')
    }
    New-Item -ItemType Directory -Force -Path $cfg | Out-Null
    $tmp = "$vdf.tmp"
    [IO.File]::WriteAllBytes($tmp, [AidynVdf]::Dump($tree))
    Move-Item -LiteralPath $tmp -Destination $vdf -Force
    $grid = Join-Path $cfg 'grid'
    New-Item -ItemType Directory -Force -Path $grid | Out-Null
    foreach ($pair in @(@('capsule.png', "${id}p.png"), @('wide.png', "$id.png"), @('hero.png', "${id}_hero.png"), @('logo.png', "${id}_logo.png"))) {
        $src = Join-Path $art $pair[0]
        if (Test-Path -LiteralPath $src) { Copy-Item -LiteralPath $src -Destination (Join-Path $grid $pair[1]) -Force }
    }
}
if ($closedSteam) {
    Start-Process -FilePath $steamExe
    Say "Done. Steam is starting again: $Name is in the Library under Non-Steam."
} else {
    Say "Done. Start Steam: $Name is in the Library under Non-Steam."
}
