# SPDX-License-Identifier: BSD-3-Clause
# Copyright Contributors to the DJV project.

<#
.SYNOPSIS
Saves a copy of the DJV sync server's database while it runs.

.DESCRIPTION
Signs in to the server as its administrator (ADMIN_USER in .env, "admin"
unless set), with the password in secrets\admin_password.txt, and saves the database the admin page's "Download
a backup" gives into backups\djv-sync-<date>.db. The copy is consistent even
while people are writing notes, and opens in any SQLite tool.

data\config.json holds the users and the settings; it is plain JSON and can
be copied as it is.

.EXAMPLE
.\backup.ps1

.EXAMPLE
.\backup.ps1 -Url http://192.168.0.100:8877 -Keep 30
#>
[CmdletBinding()]
param(
    [string]$Url = 'http://localhost:8877',
    # How many backups to keep in the folder; 0 keeps them all.
    [int]$Keep = 0
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$admin = 'admin'
if (Test-Path '.env') {
    foreach ($line in Get-Content '.env') {
        if ($line -match '^\s*ADMIN_USER\s*=\s*(.*?)\s*$' -and $Matches[1]) {
            $admin = $Matches[1]
        }
    }
}
$password = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot 'secrets\admin_password.txt')).TrimEnd("`r", "`n")
$body = @{ username = $admin; password = $password } | ConvertTo-Json
$login = Invoke-RestMethod -Method Post -Uri "$Url/api/login" -ContentType 'application/json' -Body $body
$headers = @{ Authorization = "Bearer $($login.token)" }
try {
    New-Item -ItemType Directory -Force -Path 'backups' | Out-Null
    $stamp = Get-Date -Format 'yyyy-MM-dd_HHmmss'
    $file = Join-Path $PSScriptRoot "backups\djv-sync-$stamp.db"
    Invoke-WebRequest -Uri "$Url/api/admin/backup" -Headers $headers -OutFile $file -UseBasicParsing
    Copy-Item 'data\config.json' (Join-Path $PSScriptRoot "backups\config-$stamp.json") -ErrorAction SilentlyContinue
    Write-Host "Saved $file"
} finally {
    Invoke-RestMethod -Method Post -Uri "$Url/api/logout" -Headers $headers | Out-Null
}

if ($Keep -gt 0) {
    Get-ChildItem 'backups' -Filter 'djv-sync-*.db' | Sort-Object Name -Descending |
        Select-Object -Skip $Keep | Remove-Item
}
