# SPDX-License-Identifier: BSD-3-Clause
# Copyright Contributors to the DJV project.

<#
.SYNOPSIS
Makes the DJV sync server's passwords, in the secrets folder beside this
script.

.DESCRIPTION
Writes two files, each holding one password and nothing else:

  secrets\lldjv_password.txt  The administrator's password: 24 random
                              characters. Sign in to the admin page with it,
                              as lldjv.
  secrets\share_password.txt  The password of the "djv" Windows account the
                              media share is read with: 16 random letters
                              and digits, or the one given. share-setup.ps1
                              gives the account this password, and the
                              server passes it to signed-in DJVs.

A file that is already there is kept, so that running this again changes
nothing; -Force makes new passwords, which then have to reach the server
(restart it) and the share's account (run share-setup.ps1 again).

The files are the only copy of the passwords: keep the folder private, and
out of any version control.

.EXAMPLE
.\new-secrets.ps1

.EXAMPLE
.\new-secrets.ps1 -SharePassword 'Studio-Media-2026'
#>
[CmdletBinding()]
param(
    # The share account's password, when the studio has a standard one.
    [string]$SharePassword,
    # Replace passwords that are already there.
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$dir = Join-Path $PSScriptRoot 'secrets'
New-Item -ItemType Directory -Force -Path $dir | Out-Null

$rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()

# A random password from the alphabet, drawn again until it has a character
# of each kind asked for. The symbols are ones that need no quoting in a
# command line, a JSON file, or Windows' own dialogs.
function New-Password([int]$Length, [string[]]$Kinds) {
    $alphabet = -join $Kinds
    $bytes = New-Object byte[] 4
    while ($true) {
        $chars = for ($i = 0; $i -lt $Length; ++$i) {
            $rng.GetBytes($bytes)
            $alphabet[[BitConverter]::ToUInt32($bytes, 0) % $alphabet.Length]
        }
        $password = -join $chars
        $hasAll = $true
        foreach ($kind in $Kinds) {
            if ($password.IndexOfAny($kind.ToCharArray()) -lt 0) {
                $hasAll = $false
            }
        }
        if ($hasAll) {
            return $password
        }
    }
}

$upper = 'ABCDEFGHJKLMNPQRSTUVWXYZ'
$lower = 'abcdefghijkmnopqrstuvwxyz'
$digits = '23456789'
$symbols = '-_.!@#*+=?~'

# Written as UTF-8 without a byte order mark or a line break: the file is
# the password.
function Write-Secret([string]$Name, [string]$Value) {
    $path = Join-Path $dir $Name
    if ((Test-Path $path) -and -not $Force) {
        Write-Host "Kept $path (use -Force to replace it)"
        return
    }
    [System.IO.File]::WriteAllText($path, $Value)
    Write-Host "Wrote $path"
}

Write-Secret 'lldjv_password.txt' (New-Password 24 @($upper, $lower, $digits, $symbols))

if ($SharePassword) {
    if ($SharePassword.Length -lt 8) {
        throw 'The share password needs at least 8 characters.'
    }
    $share = $SharePassword
} else {
    # Letters and digits only, so that it can be read out and typed by hand
    # when someone has to map the share without DJV.
    $share = New-Password 16 @($upper, $lower, $digits)
}
Write-Secret 'share_password.txt' $share

Write-Host ''
Write-Host "The passwords are in $dir. Keep that folder private."
