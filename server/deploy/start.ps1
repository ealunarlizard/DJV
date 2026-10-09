# SPDX-License-Identifier: BSD-3-Clause
# Copyright Contributors to the DJV project.

<#
.SYNOPSIS
Starts the DJV sync server in Docker, from this folder.

.DESCRIPTION
Loads the server's image from the package's .tar when Docker does not have
it yet, makes .env from .env.example the first time, starts the container,
and waits until the server answers.

The container restarts with Docker, so this is needed once, and again after
an update or stop.ps1.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# Docker writes its progress to stderr, which Windows PowerShell turns into
# errors -- stopping the script -- when the output is redirected. Only the
# exit code says whether it failed.
function Invoke-Docker {
    $ErrorActionPreference = 'Continue'
    & docker @args 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) {
        throw "docker $($args -join ' ') failed."
    }
}

foreach ($name in 'lldjv_password.txt', 'share_password.txt') {
    if (-not (Test-Path (Join-Path 'secrets' $name))) {
        throw "secrets\$name is missing: run .\new-secrets.ps1 first."
    }
}

if (-not (Test-Path '.env')) {
    Copy-Item '.env.example' '.env'
    Write-Host 'Made .env from .env.example.'
}
$settings = @{}
foreach ($line in Get-Content '.env') {
    if ($line -match '^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$') {
        $settings[$Matches[1]] = $Matches[2]
    }
}
$version = if ($settings['DJV_SYNC_VERSION']) { $settings['DJV_SYNC_VERSION'] } else { 'latest' }
$port = if ($settings['DJV_PORT']) { $settings['DJV_PORT'] } else { '8877' }

# The image comes with the package, so that the server needs no registry
# and no internet.
$ErrorActionPreference = 'Continue'
docker image inspect "djv-sync-server:$version" *> $null
$haveImage = $LASTEXITCODE -eq 0
$ErrorActionPreference = 'Stop'
if (-not $haveImage) {
    $tar = Get-ChildItem -Filter 'djv-sync-server-*.tar' | Sort-Object Name | Select-Object -Last 1
    if (-not $tar) {
        throw "Docker has no image djv-sync-server:$version, and there is no djv-sync-server-*.tar here to load it from."
    }
    Write-Host "Loading $($tar.Name)..."
    Invoke-Docker load -i $tar.FullName
}

New-Item -ItemType Directory -Force -Path 'data' | Out-Null
Invoke-Docker compose up -d

$url = "http://localhost:$port"
Write-Host "Waiting for $url ..."
for ($i = 0; $i -lt 30; ++$i) {
    try {
        Invoke-RestMethod "$url/api/health" -TimeoutSec 2 | Out-Null
        Write-Host "The DJV sync server is running. Admin page: $url/admin (user lldjv, password in secrets\lldjv_password.txt)"
        return
    } catch {
        Start-Sleep -Seconds 1
    }
}
Invoke-Docker compose logs --tail 30
throw "The server did not answer at $url; its log is above."
