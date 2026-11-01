# Builds Sketch3D (Release), bundles Qt with windeployqt and compiles the Inno Setup installer.
# Output: dist\Sketch3D_Setup_<version>.exe
$ErrorActionPreference = 'Stop'
$Qt    = 'C:\Qt\6.11.2\mingw_64'
$Tools = 'C:\Qt\Tools'
$ISCC  = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
$env:PATH = "$Tools\mingw1310_64\bin;$Tools\Ninja;$Tools\CMake_64\bin;$Qt\bin;$env:PATH"

$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

cmake -S . -B build\Release -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$Qt"
cmake --build build\Release --parallel

$deploy = 'deploy\Sketch3D'
if (Test-Path deploy) { Remove-Item deploy -Recurse -Force }
New-Item -ItemType Directory -Force $deploy | Out-Null
Copy-Item build\Release\Sketch3D.exe $deploy
& "$Qt\bin\windeployqt.exe" --release --no-translations --compiler-runtime "$deploy\Sketch3D.exe"

& $ISCC installer\Sketch3D.iss
