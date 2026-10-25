<#
.SYNOPSIS
Prepare and launch two private, offline Sonic R instances for manual LAN checks.
#>
[CmdletBinding()]
param(
    [string]$DataDirectory = $env:SONICR_DATA_DIR,
    [string]$Executable = (Join-Path $PSScriptRoot '../source/sdl/build/lan-verification/sonicr.exe'),
    [string]$SessionDirectory = (Join-Path $PSScriptRoot '../source/sdl/build/lan-windows'),
    [switch]$PrepareOnly,
    [switch]$Status,
    [switch]$Stop
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildDirectory = [IO.Path]::GetFullPath((Join-Path $repoDirectory 'source/sdl/build'))
$sessionPath = [IO.Path]::GetFullPath($SessionDirectory)
if (-not $sessionPath.StartsWith($buildDirectory + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'SessionDirectory must be a child of ignored source/sdl/build; private assets must stay untracked.'
}
$manifestPath = Join-Path $sessionPath 'processes.json'

function Get-OwnedProcess($Entry) {
    $process = Get-Process -Id $Entry.pid -ErrorAction SilentlyContinue
    if ($null -eq $process) { return $null }
    $recordedStart = [datetime]$Entry.startedUtc
    if ($process.Path -ne $Entry.executable -or
        $process.StartTime.ToUniversalTime().Ticks -ne $recordedStart.ToUniversalTime().Ticks) {
        throw "PID $($Entry.pid) no longer belongs to the recorded test process."
    }
    return $process
}

if ($Status -or $Stop) {
    if (-not (Test-Path -LiteralPath $manifestPath)) { throw "No process manifest: $manifestPath" }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    foreach ($entry in $manifest.processes) {
        $process = Get-OwnedProcess $entry
        if ($Stop -and $null -ne $process) {
            [void]$process.CloseMainWindow()
            if (-not $process.WaitForExit(5000)) {
                throw "Process $($entry.pid) did not close; close its game window manually."
            }
            $process = $null
        }
        $endpoints = @()
        if ($null -ne $process) {
            $endpoints = @(Get-NetUDPEndpoint -OwningProcess $entry.pid -ErrorAction SilentlyContinue |
                Select-Object LocalAddress, LocalPort)
        }
        [pscustomobject]@{
            Role = $entry.role; PID = $entry.pid; Running = ($null -ne $process)
            Executable = $entry.executable; UDP = ($endpoints | ConvertTo-Json -Compress)
            Stdout = $entry.stdout; Stderr = $entry.stderr
        }
    }
    return
}

$sourceData = (Resolve-Path -LiteralPath $DataDirectory).Path
$sourceExe = (Resolve-Path -LiteralPath $Executable).Path
if ($sessionPath.StartsWith($sourceData.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $sourceData.StartsWith($sessionPath + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Source data and test session directories must not contain one another.'
}
foreach ($folder in 'AI','BIN','CITY','EMERALD','FACTORY','GENERAL','ISLAND','RUIN','SOUND') {
    if (-not (Test-Path -LiteralPath (Join-Path $sourceData $folder) -PathType Container)) {
        throw "Missing required source data folder: $folder"
    }
}
if (Test-Path -LiteralPath $manifestPath) {
    $previous = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    foreach ($entry in $previous.processes) {
        if ($null -ne (Get-OwnedProcess $entry)) { throw 'A recorded test instance is still running; use -Status or -Stop.' }
    }
}
if (-not $PrepareOnly -and @(Get-NetUDPEndpoint -LocalPort 7847 -ErrorAction SilentlyContinue).Count) {
    throw 'UDP port 7847 is already bound. Stop the existing host before this test.'
}
[void](New-Item -ItemType Directory -Path $sessionPath -Force)
$binaryHash = (Get-FileHash -LiteralPath $sourceExe -Algorithm SHA256).Hash
$manifest = [ordered]@{
    sourceExecutable = $sourceExe; sha256 = $binaryHash
    sourceData = $sourceData; sourceCommit = (git -C $repoDirectory rev-parse HEAD)
    processes = @()
}

foreach ($role in 'host','client') {
    $rolePath = Join-Path $sessionPath $role
    $roleData = Join-Path $rolePath 'data'
    $preparedPath = Join-Path $rolePath 'prepared.json'
    [void](New-Item -ItemType Directory -Path $rolePath -Force)
    if (-not (Test-Path -LiteralPath $preparedPath)) {
        if (Test-Path -LiteralPath $roleData) {
            throw "Incomplete preparation at $roleData; inspect it before using a fresh SessionDirectory."
        }
        [void](New-Item -ItemType Directory -Path $roleData)
        foreach ($directory in Get-ChildItem -LiteralPath $sourceData -Directory) {
            Copy-Item -LiteralPath $directory.FullName -Destination $roleData -Recurse
        }
        # Copy local options, never ONLINE.DAT, executables or root credentials.
        foreach ($name in 'SONICR.INF','JOYSTICK.INF','KEYS.BIN') {
            $config = Join-Path $sourceData $name
            if (Test-Path -LiteralPath $config) { Copy-Item -LiteralPath $config -Destination $roleData }
        }
        foreach ($folder in 'SAVE','GHOST') {
            [void](New-Item -ItemType Directory -Path (Join-Path $roleData $folder) -Force)
        }
        @{ sourceData = $sourceData } | ConvertTo-Json | Set-Content -LiteralPath $preparedPath
    } else {
        $prepared = Get-Content -LiteralPath $preparedPath -Raw | ConvertFrom-Json
        if ($prepared.sourceData -ne $sourceData) { throw "Prepared source differs for $role; choose a fresh session directory." }
    }
    $roleExe = Join-Path $rolePath "sonicr-$role.exe"
    Copy-Item -LiteralPath $sourceExe -Destination $roleExe -Force
    if ((Get-FileHash -LiteralPath $roleExe -Algorithm SHA256).Hash -ne $binaryHash) {
        throw "Executable copy mismatch for $role"
    }
    if ($PrepareOnly) {
        Write-Output "Prepared $role at $rolePath (private assets remain ignored)."
        continue
    }
    $outputLog = Join-Path $rolePath 'stdout.log'
    $errorLog = Join-Path $rolePath 'stderr.log'
    $arguments = @('--lan', '--username', "LAN_$($role.ToUpperInvariant())")
    if ($role -eq 'client') { $arguments += @('--host', '127.0.0.1') }
    $arguments += '"' + $roleData + '"'
    $process = Start-Process -FilePath $roleExe -ArgumentList $arguments -WorkingDirectory $rolePath `
        -WindowStyle Normal -RedirectStandardOutput $outputLog -RedirectStandardError $errorLog -PassThru
    $manifest.processes += [ordered]@{
        role = $role; pid = $process.Id; executable = $roleExe
        startedUtc = $process.StartTime.ToUniversalTime().ToString('O')
        arguments = $arguments; stdout = $outputLog; stderr = $errorLog
    }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath
    Write-Output "Started $role PID $($process.Id); choose Network, then $(if ($role -eq 'host') { 'F1 Host' } else { 'F2 Join' })."
}
if (-not $PrepareOnly) { Write-Output "Manifest: $manifestPath. Use -Status to inspect or -Stop to close these exact processes." }
