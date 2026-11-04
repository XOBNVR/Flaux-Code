# Puts everything the Windows installer needs into build\app.
# Runs on GitHub's Windows machines (see .github/workflows/windows.yml).
#
#   build\app\flaux, flauxc\, examples\, docs\, assets\   Flaux itself
#   build\app\python\      Python, the "embeddable" version from python.org
#   build\app\toolchain\   gcc for Windows, from w64devkit (github.com/skeeto/w64devkit)
#   build\app\sdl2\        SDL2 for graphics (github.com/libsdl-org/SDL)
#   build\app\mesa\        Mesa, OpenGL without a graphics card, for 3D on PCs whose graphics
#                          are too old (github.com/pal1000/mesa-dist-win)
#   build\app\openxr\      the OpenXR loader, for VR headsets (github.com/KhronosGroup/OpenXR-SDK-Source)
#   build\app\bin\         flaux.exe, made from installer\launcher.c

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"   # makes downloads much faster
$PythonVersion = "3.12.8"

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$app = Join-Path $root "build\app"
$tmp = Join-Path $root "build\tmp"
Remove-Item -Recurse -Force (Join-Path $root "build") -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $app, $tmp, "$app\bin" | Out-Null

$headers = @{ "User-Agent" = "flaux-build" }
if ($env:GITHUB_TOKEN) { $headers["Authorization"] = "Bearer $env:GITHUB_TOKEN" }

function Download($url, $file) {
    Write-Host "Downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $file -Headers @{ "User-Agent" = "flaux-build" }
}

# 1. Flaux itself
Write-Host "Copying Flaux"
Copy-Item flaux, README.md -Destination $app
foreach ($dir in "flauxc", "examples", "docs", "assets") {
    Copy-Item -Recurse $dir -Destination $app
}
Get-ChildItem $app -Recurse -Directory -Filter "__pycache__" | Remove-Item -Recurse -Force

# 2. Python
Download "https://www.python.org/ftp/python/$PythonVersion/python-$PythonVersion-embed-amd64.zip" "$tmp\python.zip"
Expand-Archive "$tmp\python.zip" "$app\python"

# 2b. Tk, for Flaux Studio's window (flauxc\studio\app.py). The embeddable Python leaves it out,
#     so it comes from the same Python's installer part, tcltk.msi (unpacked, not installed).
Download "https://www.python.org/ftp/python/$PythonVersion/amd64/tcltk.msi" "$tmp\tcltk.msi"
$msi = Start-Process msiexec.exe -Wait -PassThru -ArgumentList "/a", "`"$tmp\tcltk.msi`"", "/qn", "TARGETDIR=`"$tmp\tcltk`""
if ($msi.ExitCode -ne 0) { throw "Couldn't unpack tcltk.msi (exit code $($msi.ExitCode))" }
Get-ChildItem "$tmp\tcltk\DLLs" -File | Where-Object { $_.Name -match '^(_tkinter\.pyd|tcl\d+t?\.dll|tk\d+t?\.dll|zlib1\.dll)$' } |
    Copy-Item -Destination "$app\python\"
Copy-Item -Recurse "$tmp\tcltk\Lib\tkinter" "$app\python\tkinter"
Copy-Item -Recurse "$tmp\tcltk\tcl" "$app\python\tcl"
Remove-Item -Recurse -Force "$app\python\tkinter\test" -ErrorAction SilentlyContinue
$tk = & "$app\python\python.exe" -c "import tkinter; r = tkinter.Tk(); r.withdraw(); print('Tk', r.tk.call('info', 'patchlevel')); r.destroy()"
if ($LASTEXITCODE -ne 0) { throw "Tk doesn't work in the bundled Python" }
Write-Host $tk

# 3. gcc (w64devkit): the newest release for 64-bit Windows
$release = Invoke-RestMethod "https://api.github.com/repos/skeeto/w64devkit/releases/latest" -Headers $headers
$asset = $release.assets | Where-Object { $_.name -match '^w64devkit-(x64-)?[\d.]+\.(exe|zip|7z\.exe)$' } | Select-Object -First 1
if (-not $asset) { throw "Couldn't find a w64devkit download in release $($release.tag_name)" }
Download $asset.browser_download_url "$tmp\$($asset.name)"
& 7z x "$tmp\$($asset.name)" "-o$tmp\w64" -y | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Couldn't unpack $($asset.name)" }
$kit = Get-ChildItem "$tmp\w64" -Recurse -Filter gcc.exe | Where-Object { $_.Directory.Name -eq "bin" } | Select-Object -First 1
Move-Item $kit.Directory.Parent.FullName "$app\toolchain"
Write-Host "gcc: $(& "$app\toolchain\bin\gcc.exe" --version | Select-Object -First 1)"

# 4. SDL2: the newest 2.x release, built for gcc (mingw)
$releases = Invoke-RestMethod "https://api.github.com/repos/libsdl-org/SDL/releases?per_page=100" -Headers $headers
$sdlAsset = $null
foreach ($r in $releases) {
    $sdlAsset = $r.assets | Where-Object { $_.name -match '^SDL2-devel-2\.[\d.]+-mingw\.zip$' } | Select-Object -First 1
    if ($sdlAsset) { break }
}
if (-not $sdlAsset) { throw "Couldn't find an SDL2 download" }
Download $sdlAsset.browser_download_url "$tmp\sdl2.zip"
Expand-Archive "$tmp\sdl2.zip" "$tmp\sdl2"
$mingw = Get-ChildItem "$tmp\sdl2" -Recurse -Directory -Filter "x86_64-w64-mingw32" | Select-Object -First 1
New-Item -ItemType Directory -Force "$app\sdl2" | Out-Null
foreach ($dir in "include", "lib", "bin") { Copy-Item -Recurse "$($mingw.FullName)\$dir" "$app\sdl2\$dir" }
Write-Host "SDL2: $($sdlAsset.name)"

# 4b. Mesa (software OpenGL): the newest release, built for gcc (mingw), 64-bit
$mesaRelease = Invoke-RestMethod "https://api.github.com/repos/pal1000/mesa-dist-win/releases/latest" -Headers $headers
$mesaAsset = $mesaRelease.assets | Where-Object { $_.name -match '^mesa3d-[\d.]+-release-mingw\.7z$' } | Select-Object -First 1
if (-not $mesaAsset) { throw "Couldn't find a Mesa download in release $($mesaRelease.tag_name)" }
Download $mesaAsset.browser_download_url "$tmp\mesa.7z"
& 7z x "$tmp\mesa.7z" "-o$tmp\mesa" -y | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Couldn't unpack $($mesaAsset.name)" }
New-Item -ItemType Directory -Force "$app\mesa" | Out-Null
foreach ($dll in "opengl32.dll", "libgallium_wgl.dll", "libglapi.dll") {
    $found = Get-ChildItem "$tmp\mesa\x64" -Filter $dll -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { Copy-Item $found.FullName "$app\mesa\" }
}
if (-not (Test-Path "$app\mesa\opengl32.dll")) { throw "Mesa's opengl32.dll is missing from $($mesaAsset.name)" }
Get-ChildItem "$tmp\mesa" -Filter "*.txt" | Select-Object -First 3 | ForEach-Object { Copy-Item $_.FullName "$app\mesa\" }
Write-Host "Mesa: $($mesaAsset.name)"

# 4c. The OpenXR loader (finds SteamVR, Meta Quest Link, Windows Mixed Reality, ...)
$xrRelease = Invoke-RestMethod "https://api.github.com/repos/KhronosGroup/OpenXR-SDK-Source/releases/latest" -Headers $headers
$xrAsset = $xrRelease.assets | Where-Object { $_.name -match '^openxr_loader_windows-[\d.]+\.zip$' } | Select-Object -First 1
if (-not $xrAsset) { throw "Couldn't find the OpenXR loader in release $($xrRelease.tag_name)" }
Download $xrAsset.browser_download_url "$tmp\openxr.zip"
Expand-Archive "$tmp\openxr.zip" "$tmp\openxr"
$xrDll = Get-ChildItem "$tmp\openxr" -Recurse -Filter "openxr_loader.dll" | Where-Object { $_.FullName -match 'x64' -and $_.FullName -notmatch 'uwp' } | Select-Object -First 1
if (-not $xrDll) { throw "openxr_loader.dll (64-bit) is missing from $($xrAsset.name)" }
New-Item -ItemType Directory -Force "$app\openxr" | Out-Null
Copy-Item $xrDll.FullName "$app\openxr\"
Get-ChildItem "$tmp\openxr" -Recurse -Filter "LICENSE*" | Select-Object -First 1 | ForEach-Object { Copy-Item $_.FullName "$app\openxr\" }
Write-Host "OpenXR loader: $($xrAsset.name) ($($xrDll.FullName))"

# 5. flaux.exe
& "$app\toolchain\bin\gcc.exe" -municode -O2 -Wall -o "$app\bin\flaux.exe" installer\launcher.c
if ($LASTEXITCODE -ne 0) { throw "Couldn't build flaux.exe" }

# 6. FlauxStudio.exe: opens Flaux Studio, the app (installer\studio_launcher.c, with the Flaux icon)
Set-Content -Path "$tmp\studio.rc" -Value "1 ICON `"$($root -replace '\\','/')/assets/flaux.ico`"" -Encoding ASCII
& "$app\toolchain\bin\windres.exe" "$tmp\studio.rc" -O coff -o "$tmp\studio-res.o"
if ($LASTEXITCODE -ne 0) { throw "Couldn't add the icon to FlauxStudio.exe" }
& "$app\toolchain\bin\gcc.exe" -municode -mwindows -O2 -Wall -o "$app\FlauxStudio.exe" installer\studio_launcher.c "$tmp\studio-res.o"
if ($LASTEXITCODE -ne 0) { throw "Couldn't build FlauxStudio.exe" }

Remove-Item -Recurse -Force $tmp
$size = (Get-ChildItem $app -Recurse | Measure-Object -Property Length -Sum).Sum / 1MB
Write-Host ("Everything is in build\app ({0:N0} MB)" -f $size)
