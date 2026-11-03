# Puts everything the Windows installer needs into build\app.
# Runs on GitHub's Windows machines (see .github/workflows/windows.yml).
#
#   build\app\flaux, flauxc\, examples\, docs\, assets\   Flaux itself
#   build\app\python\      Python, the "embeddable" version from python.org
#   build\app\toolchain\   gcc for Windows, from w64devkit (github.com/skeeto/w64devkit)
#   build\app\sdl2\        SDL2 for graphics (github.com/libsdl-org/SDL)
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

# 5. flaux.exe
& "$app\toolchain\bin\gcc.exe" -municode -O2 -Wall -o "$app\bin\flaux.exe" installer\launcher.c
if ($LASTEXITCODE -ne 0) { throw "Couldn't build flaux.exe" }

Remove-Item -Recurse -Force $tmp
$size = (Get-ChildItem $app -Recurse | Measure-Object -Property Length -Sum).Sum / 1MB
Write-Host ("Everything is in build\app ({0:N0} MB)" -f $size)
