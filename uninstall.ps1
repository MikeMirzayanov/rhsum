param(
    [switch]$User,
    [switch]$System
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$TargetName = "rhsum.exe"
$UserBinDir = if ($env:RHSUM_USER_BIN_DIR) { $env:RHSUM_USER_BIN_DIR } else { Join-Path $HOME ".local\bin" }
$SystemBinDir = if ($env:RHSUM_SYSTEM_BIN_DIR) { $env:RHSUM_SYSTEM_BIN_DIR } else { Join-Path ${env:ProgramFiles} "rhsum\bin" }

function Write-Usage {
    @"
Usage: uninstall.ps1 [-User] [-System]

Options:
  -User    Remove the user installation. Default mode.
  -System  Remove the system-wide installation.

Environment:
  RHSUM_USER_BIN_DIR Override the user installation bin directory.
  RHSUM_SYSTEM_BIN_DIR Override the system installation bin directory.
"@
}

function Test-IsAdmin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Remove-PathEntry {
    param(
        [Parameter(Mandatory = $true)][string]$PathEntry,
        [Parameter(Mandatory = $true)][ValidateSet("User", "Machine")][string]$Scope
    )

    $current = [Environment]::GetEnvironmentVariable("Path", $Scope)
    if (-not $current) {
        return
    }

    $normalizedTarget = [IO.Path]::GetFullPath($PathEntry).TrimEnd('\')
    $entries = $current.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries) |
        Where-Object { [IO.Path]::GetFullPath($_).TrimEnd('\') -ine $normalizedTarget }
    $updated = ($entries -join ';')
    [Environment]::SetEnvironmentVariable("Path", $updated, $Scope)

    $sessionEntries = $env:Path.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries) |
        Where-Object { $_ -ine $PathEntry }
    $env:Path = $sessionEntries -join ';'
}

if ($User -and $System) {
    throw "Choose only one of -User or -System."
}

$Mode = if ($System) { "System" } else { "User" }
if ($Mode -eq "System" -and -not (Test-IsAdmin)) {
    throw "System uninstall requires an elevated PowerShell session."
}

if ($Mode -eq "System") {
    Remove-Item -LiteralPath (Join-Path $SystemBinDir $TargetName) -Force -ErrorAction SilentlyContinue
    Remove-PathEntry -PathEntry $SystemBinDir -Scope Machine
    Write-Host "Removed $TargetName from $SystemBinDir"
} else {
    Remove-Item -LiteralPath (Join-Path $UserBinDir $TargetName) -Force -ErrorAction SilentlyContinue
    Remove-PathEntry -PathEntry $UserBinDir -Scope User
    Write-Host "Removed $TargetName from $UserBinDir"
}

Write-Host "rhsum uninstall completed."
