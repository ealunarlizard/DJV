# SPDX-License-Identifier: BSD-3-Clause
# Copyright Contributors to the DJV project.

<#
.SYNOPSIS
Starts the DJV sync server in Docker, from this folder.

.DESCRIPTION
Makes .env from .env.example the first time, and gets the server's image:
from the registry when DJV_SYNC_IMAGE in .env names one, or else from the
package's .tar when Docker does not have it yet. Then starts the container,
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

# Before 2.2 the administrator's password file was named after the
# administrator. Such a deployment keeps its administrator: the file is
# renamed, and its name goes into .env as ADMIN_USER.
$legacyAdmin = $null
if (-not (Test-Path 'secrets\admin_password.txt')) {
    $old = @(Get-ChildItem 'secrets' -Filter '*_password.txt' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ne 'share_password.txt' })
    if ($old.Count -eq 1) {
        $legacyAdmin = $old[0].Name -replace '_password\.txt$', ''
        Rename-Item $old[0].FullName 'admin_password.txt'
        Write-Host "Renamed secrets\$($old[0].Name) to secrets\admin_password.txt."
    }
}

foreach ($name in 'admin_password.txt', 'share_password.txt') {
    if (-not (Test-Path (Join-Path 'secrets' $name))) {
        throw "secrets\$name is missing: run .\new-secrets.ps1 first."
    }
}

if (-not (Test-Path '.env')) {
    Copy-Item '.env.example' '.env'
    Write-Host 'Made .env from .env.example.'
}
if ($legacyAdmin -and -not (Select-String -Path '.env' -Pattern '^\s*ADMIN_USER\s*=' -Quiet)) {
    Add-Content '.env' "`r`nADMIN_USER=$legacyAdmin" -Encoding ASCII
    Write-Host "Set ADMIN_USER=$legacyAdmin in .env, the administrator this deployment had."
}
$settings = @{}
foreach ($line in Get-Content '.env') {
    if ($line -match '^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$') {
        $settings[$Matches[1]] = $Matches[2]
    }
}
$version = if ($settings['DJV_SYNC_VERSION']) { $settings['DJV_SYNC_VERSION'] } else { 'latest' }
$port = if ($settings['DJV_PORT']) { $settings['DJV_PORT'] } else { '8877' }
$admin = if ($settings['ADMIN_USER']) { $settings['ADMIN_USER'] } else { 'admin' }
$repository = if ($settings['DJV_SYNC_IMAGE']) { $settings['DJV_SYNC_IMAGE'] } else { 'djv-sync-server' }
$image = "${repository}:$version"

function Test-Image([string]$Name) {
    $ErrorActionPreference = 'Continue'
    docker image inspect $Name *> $null
    return $LASTEXITCODE -eq 0
}

# With DJV_SYNC_IMAGE, the image comes from the registry, pulled at every
# start so that a "latest" there is followed; a pinned version that Docker
# already has costs only the check. The registry's sign-in is the one
# "docker login" keeps for this Windows user, which is why nothing about it
# is in .env. A registry that cannot be reached does not stop a server that
# ran before: the copy Docker has, or else the package's .tar, is used.
$haveImage = Test-Image $image
if ($repository -ne 'djv-sync-server') {
    Write-Host "Pulling $image ..."
    try {
        Invoke-Docker pull $image
        $haveImage = $true
    } catch {
        if ($haveImage) {
            Write-Warning "Could not pull $image; starting the copy Docker already has."
        } else {
            Write-Warning "Could not pull $image. If the registry refused it, sign in with ""docker login ghcr.io"" (a token that can read packages) and run this again."
        }
    }
}

# Without a registry, the image comes with the package, so that the server
# needs no internet. The package's image is named djv-sync-server; it is
# given the registry's name too when that is the one the compose file runs.
if (-not $haveImage) {
    $tar = Get-ChildItem -Filter 'djv-sync-server-*.tar' | Sort-Object Name | Select-Object -Last 1
    if (-not $tar) {
        throw "Docker has no image $image, and there is no djv-sync-server-*.tar here to load it from."
    }
    Write-Host "Loading $($tar.Name)..."
    Invoke-Docker load -i $tar.FullName
    if ($image -ne "djv-sync-server:$version") {
        Invoke-Docker tag "djv-sync-server:$version" $image
    }
}

New-Item -ItemType Directory -Force -Path 'data' | Out-Null

# The folder of review media, which the portal uploads to. The shared one is
# set by share-setup.ps1; without it, the videos go to a folder here that
# nobody else sees.
$media = $settings['DJV_MEDIA_DIR']
if ($media) {
    if (-not (Test-Path -LiteralPath $media -PathType Container)) {
        throw "The media folder $media (DJV_MEDIA_DIR in .env) is not there."
    }
} else {
    New-Item -ItemType Directory -Force -Path 'media' | Out-Null
    Write-Warning 'DJV_MEDIA_DIR is not set in .env: uploads go to the "media" folder here, which is not the media share. Run share-setup.ps1, or set it.'
}
Invoke-Docker compose up -d

$url = "http://localhost:$port"
Write-Host "Waiting for $url ..."
for ($i = 0; $i -lt 30; ++$i) {
    try {
        Invoke-RestMethod "$url/api/health" -TimeoutSec 2 | Out-Null
        Write-Host "The DJV sync server is running. Portal: $url/   Admin page: $url/admin (user $admin, password in secrets\admin_password.txt)"
        return
    } catch {
        Start-Sleep -Seconds 1
    }
}
Invoke-Docker compose logs --tail 30
throw "The server did not answer at $url; its log is above."
