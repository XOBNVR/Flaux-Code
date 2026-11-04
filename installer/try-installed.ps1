# After building FlauxSetup.exe: install it silently, then use Flaux the way
# a beginner would. Any problem stops the build with a clear message.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$failures = @()
function Check($ok, $what) {
    if ($ok) { Write-Host "OK    $what" } else { Write-Host "::error::FAILED $what"; $script:failures += $what }
}

# 1. install, with every option ticked
$setup = Start-Process "$root\dist\FlauxSetup.exe" -Wait -PassThru -ArgumentList `
    "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/TASKS=desktopicon,addtopath,associate", "/LOG=$env:TEMP\flaux-install.log"
Check ($setup.ExitCode -eq 0) "installer finished (exit code $($setup.ExitCode))"
$app = "$env:LOCALAPPDATA\Programs\Flaux"
Check (Test-Path "$app\bin\flaux.exe") "flaux.exe installed"
Check (Test-Path "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Flaux Studio.lnk") "Start menu shortcut"
Check (Test-Path "$([Environment]::GetFolderPath('Desktop'))\Flaux Studio.lnk") "Desktop shortcut"
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
Check ($userPath -like "*$app\bin*") "flaux added to PATH"
$env:Path = "$userPath;$env:Path"
$flx = (Get-ItemProperty "HKCU:\Software\Classes\.flx" -ErrorAction SilentlyContinue)."(default)"
Check ($flx -eq "Flaux.Program") ".flx files open with Flaux Studio"

# 2. the `flaux` command
$v = flaux --version
Check ($v -match "Flaux") "flaux --version says: $v"
$hello = flaux "$app\examples\hello.flx"
Check (($hello -join "`n") -match "Hello, world!") "flaux runs hello.flx"

# 3. a batch file, exactly like the README teaches
$dir = Join-Path $env:TEMP "my flaux games"
New-Item -ItemType Directory -Force $dir | Out-Null
Set-Content -Path "$dir\mygame.flx" -Value 'say "Hello from my batch file!"'
Set-Content -Path "$dir\play.bat" -Value "@echo off`r`ncd /d `"%~dp0`"`r`nflaux mygame.flx`r`npause`r`n" -NoNewline
# press a key for `pause` automatically, the way a person would
Set-Content -Path "$dir\press-a-key.cmd" -Value "@echo off`r`necho.| call `"%~dp0play.bat`"`r`n" -NoNewline
$batch = & "$dir\press-a-key.cmd" 2>&1
$text = $batch -join "`n"
Check ($text -match "Hello from my batch file!") "the README batch file runs the game"
Check ($text -match "Press any key") "...and pause still runs afterwards"

# 4. a graphics game, without a screen
$env:SDL_VIDEODRIVER = "dummy"; $env:FLAUX_FRAMES = "30"; $env:FLAUX_FAST = "1"
$env:FLAUX_SCREENSHOT = "$dir\shot.bmp"
flaux "$app\examples\games\space_dodge.flx"
Check ($LASTEXITCODE -eq 0 -and (Test-Path "$dir\shot.bmp")) "Space Dodge runs (with its pictures)"
Remove-Item Env:SDL_VIDEODRIVER, Env:FLAUX_FRAMES, Env:FLAUX_FAST, Env:FLAUX_SCREENSHOT

# 5. making an .exe to share
Push-Location $dir
flaux build mygame.flx | Out-Host
$made = & "$dir\mygame.exe"
Pop-Location
Check (($made -join "") -match "Hello from my batch file!") "flaux build makes a working .exe"

# 6. Flaux Studio starts and answers (and refuses visitors without its secret)
$studio = Start-Process "$app\python\python.exe" -PassThru -WindowStyle Hidden `
    -ArgumentList "`"$app\flaux`"", "studio", "--no-window", "--port", "8790"
Start-Sleep -Seconds 4
$code = 0
try { Invoke-WebRequest "http://127.0.0.1:8790/" -UseBasicParsing | Out-Null } catch { $code = $_.Exception.Response.StatusCode.value__ }
Check ($code -eq 403) "Flaux Studio is running and protected (answered $code)"
Stop-Process -Id $studio.Id -Force -ErrorAction SilentlyContinue

# 7. the apps: Flaux Studio, and a game made with Flaux Studio on the web, each in a window of
#    its own (WebView2, see installer\app). They need the WebView2 runtime (Windows 10 and 11
#    have it; this computer gets it here if it hasn't).
Check (Test-Path "$app\FlauxStudio.exe") "FlauxStudio.exe installed"
$guid = "{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
$wv2 = @("HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\$guid", "HKLM:\SOFTWARE\Microsoft\EdgeUpdate\Clients\$guid",
         "HKCU:\Software\Microsoft\EdgeUpdate\Clients\$guid") |
       ForEach-Object { (Get-ItemProperty $_ -ErrorAction SilentlyContinue).pv } | Where-Object { $_ -and $_ -ne "0.0.0.0" } |
       Select-Object -First 1
if (-not $wv2) {
    Write-Host "Installing the WebView2 runtime..."
    Invoke-WebRequest "https://go.microsoft.com/fwlink/p/?LinkId=2124703" -OutFile "$env:TEMP\wv2setup.exe" -UseBasicParsing
    Start-Process "$env:TEMP\wv2setup.exe" -Wait -ArgumentList "/silent", "/install" | Out-Null
}
function Wait-For($file, $seconds) {
    for ($i = 0; $i -lt $seconds -and -not (Test-Path $file); $i++) { Start-Sleep -Seconds 1 }
    if (Test-Path $file) { Get-Content $file -Raw -Encoding UTF8 } else { "" }
}
$said = "$env:TEMP\flaux-studio-app.json"
Remove-Item $said -ErrorAction SilentlyContinue
$env:FLAUX_APP_TEST = $said      # (the app writes what the page says, then closes)
$studioApp = Start-Process "$app\FlauxStudio.exe" -PassThru
$text = Wait-For $said 120
Check ($text -match "Flaux Studio \|") "Flaux Studio opens in its own window ($text)"
$studioApp.WaitForExit(15000) | Out-Null
Start-Sleep -Seconds 2
$left = Get-Process python, pythonw -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$app\*" }
Check ($studioApp.HasExited -and -not $left) "closing Flaux Studio stops Flaux too"
$left | Stop-Process -Force -ErrorAction SilentlyContinue

& "$app\python\python.exe" "$root\installer\app\build_app.py" "$dir\apps" --cxx "$app\toolchain\bin\g++.exe" `
    --windres "$app\toolchain\bin\windres.exe" | Out-Host
Set-Content -Path "$dir\player_test.flx" -Encoding UTF8 -Value @'
window(320, 200, "Player test")
clear("navy")
say.same "the game"
say " works"
show()
'@
& "$app\python\python.exe" "$root\installer\app\make_game_exe.py" "$dir\apps\FlauxPlayer.exe" "$dir\player_test.flx" `
    "$dir\Player Test.exe" | Out-Host
$said = "$env:TEMP\flaux-player-app.json"
Remove-Item $said -ErrorAction SilentlyContinue
$env:FLAUX_APP_TEST = $said
$game = Start-Process "$dir\Player Test.exe" -PassThru
$text = Wait-For $said 60
Check ($text -match '^\[0,' -and $text -match "the game works") "a game exported from the web version plays in its own window ($text)"
$game.WaitForExit(15000) | Out-Null
Check $game.HasExited "...and closes"
Remove-Item Env:FLAUX_APP_TEST

# 8. uninstall cleans up
$un = Start-Process "$app\unins000.exe" -Wait -PassThru -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"
Check ($un.ExitCode -eq 0) "uninstaller finished"
Start-Sleep -Seconds 2
Check (-not (Test-Path "$app\bin\flaux.exe")) "files removed"
Check (-not ([Environment]::GetEnvironmentVariable("Path", "User") -like "*$app\bin*")) "PATH cleaned up"

if ($failures.Count) {
    Write-Host "`nInstall log:"; Get-Content "$env:TEMP\flaux-install.log" -Tail 40
    exit 1
}
Write-Host "`nEverything worked."
