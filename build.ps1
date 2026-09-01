[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer was not found. Install Visual Studio Build Tools with Desktop development with C++.'
}

$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) {
    throw 'Visual Studio C++ build tools were not found.'
}

$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) {
    $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
}

$build = Join-Path $root 'build\x64'
& $cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $build --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$output = Join-Path $root ('out\x64\' + $Configuration.ToLowerInvariant())
New-Item -ItemType Directory -Force -Path $output | Out-Null
Copy-Item -Force (Join-Path $build "$Configuration\version.dll") $output
Copy-Item -Force (Join-Path $build "$Configuration\fuck-helium-keeper.exe") $output
Copy-Item -Force (Join-Path $root 'scripts\install.ps1') $output
Copy-Item -Force (Join-Path $root 'scripts\uninstall.ps1') $output
Write-Host "Build output: $output"

