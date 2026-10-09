# SPDX-License-Identifier: BSD-3-Clause
# Copyright Contributors to the DJV project.

<#
.SYNOPSIS
Stops the DJV sync server and removes its container.

.DESCRIPTION
Nothing is lost: the sessions, the users and the settings are in .\data,
and start.ps1 makes a new container that carries on from them. Stop the
server before copying or replacing data\djv-sync.db by hand.
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

Invoke-Docker compose down
