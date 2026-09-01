[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version = '0.1.0',
    [switch]$Pause
)

$ErrorActionPreference = 'Stop'
$script:ExitCode = 1

function Test-ExplorerLaunch {
    foreach ($argument in [Environment]::GetCommandLineArgs()) {
        $trimmed = $argument.Trim()
        if ($argument -ne $trimmed -and $trimmed -ieq $PSCommandPath) {
            return $true
        }
    }
    try {
        $current = Get-CimInstance -ClassName Win32_Process -Filter "ProcessId = $PID" -ErrorAction Stop
        if ($current.ParentProcessId) {
            $parent = Get-Process -Id $current.ParentProcessId -ErrorAction Stop
            return $parent.ProcessName -ieq 'explorer'
        }
    } catch {
        # Parent-process inspection is only a convenience for Explorer launches.
    }
    return $false
}

$script:PauseOnExit = $Pause -or (Test-ExplorerLaunch)

function Wait-IfNeeded {
    if ($script:PauseOnExit) {
        Write-Host ''
        Read-Host 'Press Enter to close this window' | Out-Null
    }
}

trap {
    Write-Host "`nERROR: $($_.Exception.Message)" -ForegroundColor Red
    if ($_.ScriptStackTrace) {
        Write-Host $_.ScriptStackTrace -ForegroundColor DarkRed
    }
    Wait-IfNeeded
    exit $script:ExitCode
}

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
& $cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 "-DFUCK_HELIUM_BUILD_VERSION=$Version"
if ($LASTEXITCODE -ne 0) {
    $script:ExitCode = $LASTEXITCODE
    throw "CMake configure failed with exit code $LASTEXITCODE."
}
& $cmake --build $build --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) {
    $script:ExitCode = $LASTEXITCODE
    throw "CMake build failed with exit code $LASTEXITCODE."
}

$output = Join-Path $root ('out\x64\' + $Configuration.ToLowerInvariant())
New-Item -ItemType Directory -Force -Path $output | Out-Null
Copy-Item -Force (Join-Path $build "$Configuration\version.dll") $output
Copy-Item -Force (Join-Path $build "$Configuration\fuck-helium-keeper.exe") $output
Copy-Item -Force (Join-Path $root 'scripts\install.ps1') $output
Copy-Item -Force (Join-Path $root 'scripts\uninstall.ps1') $output
Copy-Item -Force (Join-Path $root 'install.cmd') $output
Copy-Item -Force (Join-Path $root 'uninstall.cmd') $output
Write-Host "Build output: $output"
Wait-IfNeeded
