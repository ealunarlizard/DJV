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

The image also carries DJV itself, which the server offers the studio at
http://<server>:8877/app: updating the server is what brings everyone the
new DJV. Unless -Djv names the packages, they are the Windows installer and
zip of the version in lib\djv\Models\Version.h, from the folder
package-win.bat builds them in: DJV_BUILD_DIR when set, else build-Release
beside the repository. The script stops when there are none, rather than
quietly build an image with no DJV; -NoDjv builds one on purpose.

With -Push, the image is also pushed to a registry under the given
repository, as <version> and latest, and the package's .env.example names
it as DJV_SYNC_IMAGE, so that start.ps1 pulls it from there. The .tar stays
in the package, for a server that cannot reach the registry. Sign in to the
registry first ("docker login ghcr.io", with a token that can write
packages).

.PARAMETER Push
The repository to push the image to, for example
ghcr.io/<organization>/djv-sync-server. The registry wants it in lower case.

.PARAMETER Djv
The DJV packages to offer at /app, such as
djv-3.7.5-windows-amd64.exe and its .zip, instead of the ones found in the
build folder.

.PARAMETER NoDjv
Build the image without DJV: /app then says there is nothing to download.

.EXAMPLE
.\build-package.ps1

.EXAMPLE
.\build-package.ps1 -Push ghcr.io/<organization>/djv-sync-server

.EXAMPLE
.\build-package.ps1 -Djv D:\builds\djv-3.7.5-windows-amd64.exe
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $PSScriptRoot '..\dist'),
    [string]$Push,
    [string[]]$Djv,
    [switch]$NoDjv
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

# DJV's version as CMake reads it, and so as CPack writes it into the names
# of the packages, in lower case.
function Get-DjvVersion {
    $header = Get-Content (Join-Path $PSScriptRoot '..\lib\djv\Models\Version.h') -Raw
    $parts = foreach ($macro in 'MAJOR', 'MINOR', 'PATCH') {
        if ($header -notmatch "#define\s+DJV_VERSION_$macro\s+(\d+)") {
            throw "lib\djv\Models\Version.h has no DJV_VERSION_$macro."
        }
        $Matches[1]
    }
    $dev = if ($header -match '#define\s+DJV_VERSION_DEV\s+"([^"]*)"') { $Matches[1] } else { '' }
    return (($parts -join '.') + $dev).ToLowerInvariant()
}

# The DJV packages go to the image through a folder of their own, given to
# Docker as the "downloads" build context, so that nothing is copied into
# the source tree and the server's own build context stays as it is.
$downloads = Join-Path ([IO.Path]::GetTempPath()) "djv-sync-downloads-$PID"
if (Test-Path $downloads) {
    Remove-Item -Recurse -Force $downloads
}
New-Item -ItemType Directory -Path $downloads | Out-Null
try {
    if (-not $NoDjv) {
        if (-not $Djv) {
            $djvVersion = Get-DjvVersion
            $buildDir = if ($env:DJV_BUILD_DIR) { $env:DJV_BUILD_DIR } else { Join-Path $PSScriptRoot '..\..\build-Release' }
            $Djv = @(Get-ChildItem -LiteralPath $buildDir -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -like "djv-$djvVersion-windows-*.exe" -or $_.Name -like "djv-$djvVersion-windows-*.zip" } |
                ForEach-Object { $_.FullName })
            if (-not $Djv) {
                throw "There is no DJV $djvVersion package (djv-$djvVersion-windows-*.exe or .zip) in $buildDir. Make one with package-win.bat, set DJV_BUILD_DIR to the folder it is in, name the files with -Djv, or build without DJV with -NoDjv."
            }
        }
        foreach ($file in $Djv) {
            $item = Get-Item -LiteralPath $file
            # The server only offers plain names (server.js,
            # DOWNLOAD_NAME_PATTERN): one it would not serve is refused here,
            # rather than be missing from the page.
            if ($item.Name -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,254}$') {
                throw "$($item.Name): the server only offers files named with letters, digits, '.', '_' and '-'."
            }
            Copy-Item -LiteralPath $item.FullName $downloads
            Write-Host ("DJV for /app: {0} ({1:N0} MB)" -f $item.Name, ($item.Length / 1MB))
        }
        if (-not ($Djv | Where-Object { $_ -like '*.exe' })) {
            Write-Warning 'No installer among the DJV packages: /app offers the zip.'
        }
    } else {
        Write-Host 'Building without DJV: /app will have nothing to download.'
    }

    Write-Host "Building $image ..."
    Invoke-Docker build --pull --build-context "downloads=$downloads" -t $image -t 'djv-sync-server:latest' $PSScriptRoot
} finally {
    Remove-Item -Recurse -Force $downloads -ErrorAction SilentlyContinue
}

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
