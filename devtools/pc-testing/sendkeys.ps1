# Focus the WSLg game window and send key presses via SendInput (scan codes, so SDL sees real keys).
# usage: sendkeys.ps1 -Keys c,c,Up -DelayMs 400
param([string[]]$Keys = @('c'), [int]$DelayMs = 400, [string]$Match = 'Night in the Woods')
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class KS {
  public delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP f, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
}
"@
$script:h = $null
$cb = [KS+EP]{ param($w, $l)
  $sb = New-Object Text.StringBuilder 256; [KS]::GetWindowText($w, $sb, 256) | Out-Null
  if ([KS]::IsWindowVisible($w) -and $sb.ToString() -match $Match) { $script:h = $w }
  $true }
[KS]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if (-not $script:h) { "no window"; exit 1 }
[KS]::SetForegroundWindow($script:h) | Out-Null
Start-Sleep -Milliseconds 300
$vk = @{ Up = 0x26; Down = 0x28; Left = 0x25; Right = 0x27; Enter = 0x0D; Return = 0x0D; Space = 0x20; Escape = 0x1B; Esc = 0x1B; Tab = 0x09; Shift = 0x10 }
$ext = @(0x25, 0x26, 0x27, 0x28)
foreach ($k in $Keys) {
  if ($k -match '^hold:(\w+):(\d+)$') { $name = $Matches[1]; $hold = [int]$Matches[2] } else { $name = $k; $hold = 120 }
  $code = if ($vk.ContainsKey($name)) { $vk[$name] } else { [byte][char]$name.ToUpper() }
  $scan = [byte][KS]::MapVirtualKey($code, 0)
  $f = if ($ext -contains $code) { 1 } else { 0 }
  [KS]::keybd_event([byte]$code, $scan, $f, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds $hold
  [KS]::keybd_event([byte]$code, $scan, ($f -bor 2), [UIntPtr]::Zero)
  Start-Sleep -Milliseconds $DelayMs
}
"sent $($Keys -join ',')"
