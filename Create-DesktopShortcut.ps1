# FocusShield Desktop Shortcut Installer
$WshShell = New-Object -ComObject WScript.Shell
$DesktopPath = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::Desktop)
$ShortcutPath = Join-Path $DesktopPath "FocusShield Blocker.lnk"
$TargetExe = Join-Path $PSScriptRoot "FocusShield.exe"

$Shortcut = $WshShell.CreateShortcut($ShortcutPath)
$Shortcut.TargetPath = $TargetExe
$Shortcut.WorkingDirectory = $PSScriptRoot
$Shortcut.Description = "FocusShield - Native Windows C++ Network Blocker"
$Shortcut.IconLocation = "shell32.dll,47" # Shield icon
$Shortcut.Save()

Write-Host "======================================================"
Write-Host " [FocusShield] Native Desktop shortcut created!" -ForegroundColor Green
Write-Host " Target: $TargetExe"
Write-Host " Shortcut: $ShortcutPath"
Write-Host " Double-click to launch the native C++ desktop app."
Write-Host "======================================================"
