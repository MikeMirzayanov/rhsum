param(
    [switch]$User,
    [switch]$System,
    [switch]$Keep
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoOwner = if ($env:RHSUM_REPO_OWNER) { $env:RHSUM_REPO_OWNER } else { "MikeMirzayanov" }
$RepoName = if ($env:RHSUM_REPO_NAME) { $env:RHSUM_REPO_NAME } else { "rhsum" }
$RepoRef = if ($env:RHSUM_REF) { $env:RHSUM_REF } else { "main" }
$DefaultZipUrl = "https://github.com/$RepoOwner/$RepoName/archive/refs/heads/$RepoRef.zip"
$ZipUrl = if ($env:RHSUM_ZIP_URL) { $env:RHSUM_ZIP_URL } else { $DefaultZipUrl }

$TargetName = "rhsum.exe"
$UserBinDir = if ($env:RHSUM_USER_BIN_DIR) { $env:RHSUM_USER_BIN_DIR } else { Join-Path $HOME ".local\bin" }
$SystemBinDir = if ($env:RHSUM_SYSTEM_BIN_DIR) { $env:RHSUM_SYSTEM_BIN_DIR } else { Join-Path ${env:ProgramFiles} "rhsum\bin" }

function Write-Usage {
    @"
Usage: install.ps1 [-User] [-System] [-Keep]

Options:
  -User    Install for the current user. Default mode.
  -System  Install system-wide for all users.
  -Keep    Do not delete the temporary build directory.

Environment:
  RHSUM_REPO_OWNER   GitHub owner. Default: MikeMirzayanov
  RHSUM_REPO_NAME    Repository name. Default: rhsum
  RHSUM_REF          Git ref to download. Default: main
  RHSUM_ZIP_URL      Override the ZIP URL completely.
  RHSUM_USER_BIN_DIR Override the user installation bin directory.
  RHSUM_SYSTEM_BIN_DIR Override the system installation bin directory.
"@
}

function Test-IsAdmin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-PythonCommand {
    if (Get-Command py -ErrorAction SilentlyContinue) {
        return @("py", "-3")
    }
    if (Get-Command python -ErrorAction SilentlyContinue) {
        return @("python")
    }
    throw "Python 3 was not found in PATH."
}

function Invoke-Python {
    param(
        [Parameter(Mandatory = $true)][string[]]$Args
    )

    if ($script:PythonCommand.Length -gt 1) {
        & $script:PythonCommand[0] $script:PythonCommand[1] @Args
    } else {
        & $script:PythonCommand[0] @Args
    }

    if ($LASTEXITCODE -ne 0) {
        throw "Python command failed with exit code ${LASTEXITCODE}: $($Args -join ' ')"
    }
}

function Add-PathEntry {
    param(
        [Parameter(Mandatory = $true)][string]$PathEntry,
        [Parameter(Mandatory = $true)][ValidateSet("User", "Machine")][string]$Scope
    )

    $current = [Environment]::GetEnvironmentVariable("Path", $Scope)
    $entries = @()
    if ($current) {
        $entries = $current.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries)
    }

    $normalizedTarget = [IO.Path]::GetFullPath($PathEntry).TrimEnd('\')
    foreach ($entry in $entries) {
        if ([IO.Path]::GetFullPath($entry).TrimEnd('\') -ieq $normalizedTarget) {
            return
        }
    }

    $updated = if ($current) { "$current;$PathEntry" } else { $PathEntry }
    [Environment]::SetEnvironmentVariable("Path", $updated, $Scope)
}

if ($User -and $System) {
    throw "Choose only one of -User or -System."
}

$Mode = if ($System) { "System" } else { "User" }
if ($Mode -eq "System" -and -not (Test-IsAdmin)) {
    throw "System installation requires an elevated PowerShell session."
}

$script:PythonCommand = Get-PythonCommand
$workDir = Join-Path ([IO.Path]::GetTempPath()) ([IO.Path]::GetRandomFileName())
$null = New-Item -ItemType Directory -Path $workDir

try {
    $zipPath = Join-Path $workDir "rhsum.zip"
    $extractDir = Join-Path $workDir "src"
    $null = New-Item -ItemType Directory -Path $extractDir

    Write-Host "Downloading $RepoOwner/$RepoName@$RepoRef ..."
    Invoke-WebRequest -Uri $ZipUrl -OutFile $zipPath
    Expand-Archive -Path $zipPath -DestinationPath $extractDir -Force

    $srcDir = Get-ChildItem -Path $extractDir -Recurse -Filter build.py -File |
        Where-Object { $_.DirectoryName -like "*\scripts" } |
        Select-Object -First 1 |
        ForEach-Object { Split-Path $_.DirectoryName -Parent }

    if (-not $srcDir) {
        throw "Downloaded archive does not look like the rhsum project."
    }

    Write-Host "Building and testing in $srcDir ..."
    Invoke-Python -Args @((Join-Path $srcDir "scripts\build.py"))
    Invoke-Python -Args @((Join-Path $srcDir "scripts\run_tests.py"))

    $binaryPath = Join-Path $srcDir $TargetName
    if (-not (Test-Path $binaryPath)) {
        throw "Build did not produce $TargetName."
    }

    if ($Mode -eq "System") {
        $targetDir = $SystemBinDir
        $pathScope = "Machine"
        Write-Host "Installing system-wide ..."
    } else {
        $targetDir = $UserBinDir
        $pathScope = "User"
        Write-Host "Installing for the current user ..."
    }

    $null = New-Item -ItemType Directory -Path $targetDir -Force
    Copy-Item -Path $binaryPath -Destination (Join-Path $targetDir $TargetName) -Force
    Add-PathEntry -PathEntry $targetDir -Scope $pathScope

    if (-not ($env:Path.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries) | Where-Object { $_ -ieq $targetDir })) {
        $env:Path = "$targetDir;$env:Path"
    }

    Write-Host "Installed $TargetName to $targetDir"
    Write-Host "Open a new shell to pick up the updated PATH."
    Write-Host "rhsum installation completed."
}
finally {
    if (-not $Keep -and (Test-Path $workDir)) {
        Remove-Item -LiteralPath $workDir -Recurse -Force
    } elseif ($Keep) {
        Write-Host "Kept work tree: $workDir"
    }
}
