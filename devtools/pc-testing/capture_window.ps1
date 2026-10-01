# Capture the first visible window whose title matches -Match into -Out (PNG).
param([string]$Match = 'Night in the Woods', [string]$Out = 'capture.png')
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class WinCap {
  public delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP f, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  public struct RECT { public int L, T, R, B; }
}
"@
$script:found = $null
$cb = [WinCap+EP]{ param($h, $l)
  $sb = New-Object Text.StringBuilder 256
  [WinCap]::GetWindowText($h, $sb, 256) | Out-Null
  if ([WinCap]::IsWindowVisible($h) -and $sb.ToString() -match $Match) { $script:found = $h; $script:title = $sb.ToString() }
  $true }
[WinCap]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if (-not $script:found) { "no window matching '$Match'"; exit 1 }
[WinCap]::SetForegroundWindow($script:found) | Out-Null
Start-Sleep -Milliseconds 700
$r = New-Object WinCap+RECT
[WinCap]::GetWindowRect($script:found, [ref]$r) | Out-Null
$bmp = New-Object Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
[Drawing.Graphics]::FromImage($bmp).CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
$bmp.Save($Out)
"captured '$($script:title)' $($r.R - $r.L)x$($r.B - $r.T) -> $Out"
