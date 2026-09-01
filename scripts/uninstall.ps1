[CmdletBinding()]
param([string]$Target)

$ErrorActionPreference = 'Stop'
function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

$states = @(
    (Join-Path $env:LOCALAPPDATA 'fuck-helium'),
    (Join-Path $env:ProgramFiles 'fuck-helium')
)
$state = $states | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'target.txt') } | Select-Object -First 1
if (-not $Target -and $state) {
    $Target = (Get-Content -LiteralPath (Join-Path $state 'target.txt') -Raw).Trim()
}

$isSystemInstall = $Target -and $Target.StartsWith($env:ProgramFiles, [StringComparison]::OrdinalIgnoreCase)
if ($isSystemInstall -and -not (Test-Administrator)) {
    $arguments = "-NoLogo -NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Target `"$Target`""
    $process = Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -Wait -PassThru
    exit $process.ExitCode
}

$taskName = 'fuck-helium update guard'
if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
    Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
}

if ($Target) {
    $installed = Join-Path $Target 'version.dll'
    $payload = Join-Path $state 'fuck-helium.dll'
    if ((Test-Path -LiteralPath $installed) -and (Test-Path -LiteralPath $payload)) {
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $installed).Hash -eq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $payload).Hash) {
            Remove-Item -LiteralPath $installed -Force
        } else {
            Write-Warning "Not removing $installed because it is not the DLL installed by fuck-helium."
        }
    }
}

if ($state -and (Test-Path -LiteralPath $state)) {
    Remove-Item -LiteralPath $state -Recurse -Force
}
Write-Host 'fuck-helium has been uninstalled.'
