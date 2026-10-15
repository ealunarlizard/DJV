# SPDX-License-Identifier: BSD-3-Clause
# Copyright Contributors to the DJV project.

<#
.SYNOPSIS
Builds the DJV sync server's Docker image, and the package that installs it
on a server.

.DESCRIPTION
Builds djv-sync-server:<version> (the version is package.json's) and makes
dist\djv-sync-server-<version>.zip, beside the repository's other build
output. The zip holds:

  - the image, as saved by "docker save", so the server needs neither the
    source nor a registry nor the internet;
  - deploy\: docker-compose.yml, .env.example, and the start, stop, backup,
    secrets and share scripts;
  - the setup guides from docs\lan.

Never the secrets, the data or the backups of a deployment made in deploy\.

With -Push, the image is also pushed to a registry under the given
repository, as <version> and latest, and the package's .env.example names
it as DJV_SYNC_IMAGE, so that start.ps1 pulls it from there. The .tar stays
in the package, for a server that cannot reach the registry. Sign in to the
registry first ("docker login ghcr.io", with a token that can write
packages).

.PARAMETER Push
The repository to push the image to, for example
ghcr.io/<organization>/djv-sync-server. The registry wants it in lower case.

.EXAMPLE
.\build-package.ps1

.EXAMPLE
.\build-package.ps1 -Push ghcr.io/<organization>/djv-sync-server
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $PSScriptRoot '..\dist'),
    [string]$Push
)

$ErrorActionPreference = 'Stop'

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

$version = (Get-Content (Join-Path $PSScriptRoot 'package.json') -Raw | ConvertFrom-Json).version
$image = "djv-sync-server:$version"
$name = "djv-sync-server-$version"

Write-Host "Building $image ..."
Invoke-Docker build --pull -t $image -t 'djv-sync-server:latest' $PSScriptRoot

if ($Push) {
    # Both tags are pushed: <version> for the deployments that pin it, as a
    # package's .env does, and latest for the ones that follow it.
    foreach ($tag in $version, 'latest') {
        $remote = "${Push}:$tag"
        Write-Host "Pushing $remote ..."
        Invoke-Docker tag $image $remote
        Invoke-Docker push $remote
    }
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).ProviderPath
$stage = Join-Path $OutDir $name
if (Test-Path $stage) {
    Remove-Item -Recurse -Force $stage
}
New-Item -ItemType Directory -Path $stage | Out-Null

$deploy = Join-Path $PSScriptRoot 'deploy'
foreach ($file in 'docker-compose.yml', 'start.ps1', 'stop.ps1', 'backup.ps1', 'new-secrets.ps1', 'share-setup.ps1', 'README.md') {
    Copy-Item (Join-Path $deploy $file) $stage
}
# The package runs the image it carries, rather than whatever "latest" is,
# and pulls it from the registry it was pushed to, if any.
(Get-Content (Join-Path $deploy '.env.example')) `
    -replace '^DJV_SYNC_VERSION=.*$', "DJV_SYNC_VERSION=$version" `
    -replace '^DJV_SYNC_IMAGE=.*$', "DJV_SYNC_IMAGE=$Push" |
    Set-Content (Join-Path $stage '.env.example') -Encoding ASCII

$docs = Join-Path $stage 'docs'
New-Item -ItemType Directory -Path $docs | Out-Null
foreach ($file in 'docker-deploy.md', 'share-setup.md', 'portal.md') {
    Copy-Item (Join-Path $PSScriptRoot "..\docs\lan\$file") $docs
}

Write-Host "Saving $image ..."
Invoke-Docker save -o (Join-Path $stage "$name.tar") $image

$zip = Join-Path $OutDir "$name.zip"
if (Test-Path $zip) {
    Remove-Item -Force $zip
}
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Remove-Item -Recurse -Force $stage

Write-Host "Made $zip"
