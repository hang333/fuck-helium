[CmdletBinding()]
param(
    [string]$Target,
    [switch]$NoUpdateGuard,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

function Find-Artifact([string]$Name) {
    $locations = @(
        (Join-Path $PSScriptRoot $Name),
        (Join-Path (Split-Path $PSScriptRoot -Parent) "out\x64\release\$Name")
    )
    foreach ($location in $locations) {
        if (Test-Path -LiteralPath $location) { return (Resolve-Path -LiteralPath $location).Path }
    }
    throw "$Name was not found. Run build.cmd first or use a packaged release."
}

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not $Target) {
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA 'imput\Helium\Application'),
        (Join-Path $env:ProgramFiles 'imput\Helium\Application')
    )
    $Target = $candidates | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'chrome.exe') } | Select-Object -First 1
}
if (-not $Target -or -not (Test-Path -LiteralPath (Join-Path $Target 'chrome.exe'))) {
    throw 'Helium was not found. Pass its Application directory with -Target.'
}
$Target = (Resolve-Path -LiteralPath $Target).Path

$isSystemInstall = $Target.StartsWith($env:ProgramFiles, [StringComparison]::OrdinalIgnoreCase)
if ($isSystemInstall -and -not (Test-Administrator)) {
    $arguments = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Target', "`"$Target`"")
    if ($NoUpdateGuard) { $arguments += '-NoUpdateGuard' }
    if ($Force) { $arguments += '-Force' }
    $process = Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -Wait -PassThru
    exit $process.ExitCode
}

$dll = Find-Artifact 'version.dll'
$keeper = Find-Artifact 'fuck-helium-keeper.exe'
$installedDll = Join-Path $Target 'version.dll'
if ((Test-Path -LiteralPath $installedDll) -and -not $Force) {
    $existing = (Get-FileHash -Algorithm SHA256 -LiteralPath $installedDll).Hash
    $incoming = (Get-FileHash -Algorithm SHA256 -LiteralPath $dll).Hash
    if ($existing -ne $incoming) {
        throw "A different version.dll already exists at $installedDll. Use -Force only after checking what installed it."
    }
}

$state = if ($isSystemInstall) {
    Join-Path $env:ProgramFiles 'fuck-helium'
} else {
    Join-Path $env:LOCALAPPDATA 'fuck-helium'
}
$taskName = 'fuck-helium update guard'
if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
    Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force -Path $state | Out-Null
$payload = Join-Path $state 'fuck-helium.dll'
$installedKeeper = Join-Path $state 'fuck-helium-keeper.exe'
Copy-Item -Force -LiteralPath $dll -Destination $payload
Copy-Item -Force -LiteralPath $keeper -Destination $installedKeeper
Copy-Item -Force -LiteralPath $dll -Destination $installedDll
Set-Content -LiteralPath (Join-Path $state 'target.txt') -Value $Target -Encoding Unicode

if (-not $NoUpdateGuard) {
    $quotedPayload = '"' + $payload + '"'
    $quotedTarget = '"' + $Target + '"'
    $action = New-ScheduledTaskAction -Execute $installedKeeper -Argument "$quotedPayload $quotedTarget"
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User ([Security.Principal.WindowsIdentity]::GetCurrent().Name)
    $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)
    $runLevel = if ($isSystemInstall) { 'Highest' } else { 'Limited' }
    $principal = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive -RunLevel $runLevel
    Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings -Principal $principal -Force | Out-Null
    Start-ScheduledTask -TaskName $taskName
}

Write-Host "Installed fuck-helium into $Target"
Write-Host 'Restart Helium completely before testing the menu.'
if (-not $NoUpdateGuard) {
    Write-Host 'The update guard is active and will restore version.dll if an updater removes it.'
}
