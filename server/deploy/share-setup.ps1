# SPDX-License-Identifier: BSD-3-Clause
# Copyright Contributors to the DJV project.

<#
.SYNOPSIS
Shares a folder of review media, read-only, for DJV.

.DESCRIPTION
Run as an administrator on the server that holds the media. It:

  1. Makes the local Windows account "djv" (or the one given), with the
     password in secrets\share_password.txt, which never expires. An account
     that is already there gets that password.
  2. Lets the account read the folder (NTFS: read and execute).
  3. Shares the folder, read-only for the account. Administrators keep full
     access, so that media can still be added from the server itself.
  4. Opens the Windows firewall for file sharing and for the sync server's
     port, on private and domain networks.
  5. Sets DJV_MEDIA_DIR in .env to the folder, so that the sync server's
     portal uploads videos into it. The server writes to the folder
     itself, from its container; the share stays read-only.

DJV then maps the share with this account: the sync server tells a signed-in
DJV the account and its password. See docs/lan/share-setup.md.

-Remove undoes steps 1 to 4, and leaves the folder and its files alone.
-WhatIf shows what would be done.

.EXAMPLE
.\share-setup.ps1 -Path D:\Media

.EXAMPLE
.\share-setup.ps1 -Path D:\Media -ShareName media -WhatIf

.EXAMPLE
.\share-setup.ps1 -Path D:\Media -Remove
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [string]$ShareName = 'media',
    [string]$User = 'djv',
    [string]$PasswordFile = (Join-Path $PSScriptRoot 'secrets\share_password.txt'),
    # The sync server's port, opened in the firewall.
    [int]$Port = 8877,
    [switch]$Remove
)

$ErrorActionPreference = 'Stop'

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this from a PowerShell started as an administrator.'
}

$account = "$env:COMPUTERNAME\$User"
$firewallRule = 'DJV sync server'
# The "File and Printer Sharing" rules, by the group's resource name, which
# is the same whatever the language of Windows.
$fileSharingGroup = '@FirewallAPI.dll,-28502'

if ($Remove) {
    if (Get-SmbShare -Name $ShareName -ErrorAction SilentlyContinue) {
        if ($PSCmdlet.ShouldProcess("\\$env:COMPUTERNAME\$ShareName", 'Stop sharing')) {
            Remove-SmbShare -Name $ShareName -Force
        }
    }
    if ((Test-Path $Path) -and $PSCmdlet.ShouldProcess($Path, "Remove $account from the folder's permissions")) {
        icacls $Path /remove:g $account /T /C /Q | Out-Null
    }
    if (Get-LocalUser -Name $User -ErrorAction SilentlyContinue) {
        if ($PSCmdlet.ShouldProcess($account, 'Remove the local account')) {
            Remove-LocalUser -Name $User
        }
    }
    if (Get-NetFirewallRule -DisplayName $firewallRule -ErrorAction SilentlyContinue) {
        if ($PSCmdlet.ShouldProcess($firewallRule, 'Remove the firewall rule')) {
            Remove-NetFirewallRule -DisplayName $firewallRule
        }
    }
    Write-Host 'Done. The folder and its files are as they were; file sharing stays open in the firewall.'
    return
}

if (-not (Test-Path $PasswordFile)) {
    throw "$PasswordFile is missing: run .\new-secrets.ps1 first."
}
$password = [System.IO.File]::ReadAllText($PasswordFile).TrimEnd("`r", "`n")
if ($password.Length -lt 8) {
    throw "The password in $PasswordFile is shorter than 8 characters."
}
$secure = ConvertTo-SecureString $password -AsPlainText -Force

# 1. The account. It cannot change its password, which the server hands to
# every DJV, and the password does not expire under the PCs' feet.
if (Get-LocalUser -Name $User -ErrorAction SilentlyContinue) {
    if ($PSCmdlet.ShouldProcess($account, 'Set the password from the secrets file')) {
        Set-LocalUser -Name $User -Password $secure -PasswordNeverExpires $true -UserMayChangePassword $false
        Write-Host "Set the password of $account."
    }
} elseif ($PSCmdlet.ShouldProcess($account, 'Make the local account')) {
    New-LocalUser -Name $User -Password $secure -PasswordNeverExpires -UserMayNotChangePassword `
        -AccountNeverExpires -FullName 'DJV media' -Description 'Reads the DJV review media share.' | Out-Null
    Write-Host "Made $account."
}

# 2. The folder, readable by the account.
if (-not (Test-Path $Path)) {
    if ($PSCmdlet.ShouldProcess($Path, 'Make the folder')) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
}
$fullPath = if (Test-Path $Path) { (Resolve-Path $Path).ProviderPath } else { $Path }
# Docker Desktop mounts the folder into the server's container, which it can
# only do for a folder on a drive of this machine.
if ($fullPath -notmatch '^[A-Za-z]:\\') {
    throw "$fullPath is not on a drive of this machine: the sync server could not upload to it. Give a folder like D:\Media."
}
if ($PSCmdlet.ShouldProcess($fullPath, "Let $account read the folder")) {
    icacls $fullPath /grant "${account}:(OI)(CI)RX" /C /Q | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "icacls could not give $account read access to $fullPath." }
}

# 3. The share, read-only for the account.
$share = Get-SmbShare -Name $ShareName -ErrorAction SilentlyContinue
if ($share) {
    if ($share.Path -ne $fullPath) {
        throw "There is already a share called $ShareName, of $($share.Path). Give another -ShareName, or remove that share."
    }
    if ($PSCmdlet.ShouldProcess("\\$env:COMPUTERNAME\$ShareName", "Give $account read access")) {
        Grant-SmbShareAccess -Name $ShareName -AccountName $account -AccessRight Read -Force | Out-Null
    }
} elseif ($PSCmdlet.ShouldProcess("\\$env:COMPUTERNAME\$ShareName", "Share $fullPath")) {
    New-SmbShare -Name $ShareName -Path $fullPath -ReadAccess $account -FullAccess 'BUILTIN\Administrators' `
        -Description 'DJV review media (read-only)' | Out-Null
}

# 4. The firewall.
if ($PSCmdlet.ShouldProcess('File and Printer Sharing', 'Allow on private and domain networks')) {
    Get-NetFirewallRule -Group $fileSharingGroup | Where-Object { $_.Profile -ne 'Public' } | Enable-NetFirewallRule
}
if (-not (Get-NetFirewallRule -DisplayName $firewallRule -ErrorAction SilentlyContinue)) {
    if ($PSCmdlet.ShouldProcess("TCP $Port", 'Allow the sync server on private and domain networks')) {
        New-NetFirewallRule -DisplayName $firewallRule -Direction Inbound -Protocol TCP -LocalPort $Port `
            -Action Allow -Profile Private, Domain | Out-Null
    }
}

# 5. The sync server's media folder.
$envFile = Join-Path $PSScriptRoot '.env'
if ($PSCmdlet.ShouldProcess($envFile, "Set DJV_MEDIA_DIR=$fullPath")) {
    if (-not (Test-Path $envFile)) {
        Copy-Item (Join-Path $PSScriptRoot '.env.example') $envFile
    }
    $lines = @(Get-Content $envFile)
    if ($lines -match '^\s*DJV_MEDIA_DIR\s*=') {
        $lines = $lines -replace '^\s*DJV_MEDIA_DIR\s*=.*$', "DJV_MEDIA_DIR=$fullPath"
    } else {
        $lines += "DJV_MEDIA_DIR=$fullPath"
    }
    Set-Content -Path $envFile -Value $lines -Encoding ASCII
    Write-Host "Set DJV_MEDIA_DIR=$fullPath in .env; restart the sync server (.\stop.ps1, .\start.ps1) for it to take effect."
}

$addresses = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } |
    Select-Object -ExpandProperty IPAddress
Write-Host ''
Write-Host 'The share is ready. On the admin page, set:'
foreach ($address in $addresses) {
    Write-Host "  Media share (UNC):  \\$address\$ShareName"
}
Write-Host "  Share user:         $User"
Write-Host ''
Write-Host 'To check it from a PC (then remove the test mapping with "net use \\<address>\' + $ShareName + ' /delete"):'
Write-Host "  net use \\<address>\$ShareName /user:$User"
