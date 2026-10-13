# Construit Block Launcher en Release, prépare le dossier dist/ (windeployqt) puis compile
# l'installateur Windows avec Inno Setup.
#
# Prérequis : CMake + Ninja + MSVC (Visual Studio), Qt 6 (variable $QtDir ci-dessous) et
# Inno Setup 6 (https://jrsoftware.org/isdl.php, ou "winget install JRSoftware.InnoSetup").

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$QtDir = "C:\Qt\6.8.3\msvc2022_64"
$VsVcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
$CMake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$Ninja = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$Iscc = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"

Set-Location $root

Write-Host "== Configuration CMake ==" -ForegroundColor Cyan
$configureBat = @"
@echo off
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%
call "$VsVcvars"
set PATH=$Ninja;%PATH%
"$CMake" -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$QtDir" -DCMAKE_BUILD_TYPE=Release
"@
Set-Content -Path "$root\_ci_configure.bat" -Value $configureBat -Encoding ASCII
cmd.exe /c "$root\_ci_configure.bat"
if ($LASTEXITCODE -ne 0) { throw "Échec de la configuration CMake" }

Write-Host "== Compilation ==" -ForegroundColor Cyan
$buildBat = @"
@echo off
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%
call "$VsVcvars"
set PATH=$Ninja;%PATH%
"$CMake" --build build --config Release
"@
Set-Content -Path "$root\_ci_build.bat" -Value $buildBat -Encoding ASCII
cmd.exe /c "$root\_ci_build.bat"
if ($LASTEXITCODE -ne 0) { throw "Échec de la compilation" }

Write-Host "== Préparation de dist/ (windeployqt) ==" -ForegroundColor Cyan
$dist = "$root\dist"
Remove-Item -Recurse -Force $dist -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item "$root\build\BlockLauncher.exe" "$dist\BlockLauncher.exe"
& "$QtDir\bin\windeployqt.exe" "$dist\BlockLauncher.exe" --release --no-translations --no-compiler-runtime
if (-not (Test-Path "$dist\Qt6Xml.dll")) {
    Copy-Item "$QtDir\bin\Qt6Xml.dll" "$dist\Qt6Xml.dll" -Force
}

Write-Host "== Compilation de l'installateur (Inno Setup) ==" -ForegroundColor Cyan
if (-not (Test-Path $Iscc)) { throw "Inno Setup introuvable ($Iscc). Installe-le : winget install JRSoftware.InnoSetup" }
& $Iscc "$root\installer\BlockLauncher.iss"
if ($LASTEXITCODE -ne 0) { throw "Échec de la compilation de l'installateur" }

Remove-Item "$root\_ci_configure.bat", "$root\_ci_build.bat" -ErrorAction SilentlyContinue
Write-Host "Terminé : installer\Output\BlockLauncherSetup.exe" -ForegroundColor Green
