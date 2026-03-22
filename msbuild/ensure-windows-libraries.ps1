<#
.SYNOPSIS
    Ensures all Windows build dependencies are in place before MSBuild compiles.

.DESCRIPTION
    Called by msbuild/Directory.Build.targets when the AppVeyor auto-msbuild
    runs before CI lifecycle scripts have had a chance to set up dependencies.
    Safe to run repeatedly; every step is guarded by a Test-Path check.

.PARAMETER MsbuildDir
    Path to the msbuild/ directory.  Defaults to the directory containing
    this script ($PSScriptRoot).
#>
param(
    [string]$MsbuildDir = $PSScriptRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot  = Split-Path $MsbuildDir -Parent
$WinLibDir = Join-Path $MsbuildDir 'Windows Libraries'

# ---------------------------------------------------------------------------
# 1. Download and extract Windows Libraries (provides json/json.h, lua.h, etc.)
# ---------------------------------------------------------------------------
if (-not (Test-Path (Join-Path $WinLibDir 'include\json\json.h'))) {
    Write-Host '=== Downloading Windows Libraries ==='
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $archive = Join-Path $MsbuildDir 'WindowsLibraries.7z'
    (New-Object Net.WebClient).DownloadFile(
        'https://raw.githubusercontent.com/domoticz/win32-libraries/master/WindowsLibraries.7z',
        $archive
    )

    # Locate 7-Zip (in PATH or at the standard install location)
    $7z = if (Get-Command 7z -ErrorAction SilentlyContinue) {
        '7z'
    } else {
        'C:\Program Files\7-Zip\7z.exe'
    }
    if (-not (Get-Command $7z -ErrorAction SilentlyContinue) -and -not (Test-Path $7z)) {
        throw "7-Zip not found.  Install 7-Zip or add it to PATH."
    }

    Push-Location $MsbuildDir
    try {
        & $7z x WindowsLibraries.7z | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "7z extraction failed (exit $LASTEXITCODE)" }
    } finally {
        Pop-Location
    }
    Write-Host '=== Windows Libraries downloaded and extracted ==='
}

# ---------------------------------------------------------------------------
# 2. Initialise required git submodules (jwtcpp headers, libwebem headers)
# ---------------------------------------------------------------------------
$jwtHeader = Join-Path $RepoRoot 'extern\jwtcpp\include\jwt-cpp\jwt.h'
if (-not (Test-Path $jwtHeader)) {
    Write-Host '=== Initialising git submodules ==='
    Push-Location $RepoRoot
    try {
        git submodule update --init -- extern/jwtcpp extern/libwebem
        if ($LASTEXITCODE -ne 0) { throw "git submodule update failed (exit $LASTEXITCODE)" }
    } finally {
        Pop-Location
    }
    Write-Host '=== Submodules initialised ==='
}

# ---------------------------------------------------------------------------
# 3. Write Directory.Build.props for libwebem so it finds Windows Library headers
# ---------------------------------------------------------------------------
$propsFile = Join-Path $RepoRoot 'extern\libwebem\Directory.Build.props'
if (-not (Test-Path $propsFile)) {
    Write-Host '=== Writing extern\libwebem\Directory.Build.props ==='
    $winLibInclude = Join-Path $WinLibDir 'include'
    @"
<Project>
  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalIncludeDirectories>$winLibInclude;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
"@ | Set-Content -Path $propsFile -Encoding UTF8
    Write-Host '=== Directory.Build.props written ==='
}

# ---------------------------------------------------------------------------
# 4. Build webem.lib (required for linking domoticz.exe)
# ---------------------------------------------------------------------------
$webemLib = Join-Path $RepoRoot 'extern\libwebem\build\Win32\Release\webem.lib'
if (-not (Test-Path $webemLib)) {
    Write-Host '=== Building webem.lib ==='

    # Locate MSBuild (should be in PATH on AppVeyor; fall back to vswhere)
    $msbuildExe = 'msbuild'
    if (-not (Get-Command msbuild -ErrorAction SilentlyContinue)) {
        $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $vsWhere) {
            $found = & $vsWhere -latest -requires Microsoft.Component.MSBuild `
                       -find 'MSBuild\**\Bin\MSBuild.exe' 2>$null | Select-Object -First 1
            if ($found) { $msbuildExe = $found }
        }
    }

    $webemProj = Join-Path $RepoRoot 'extern\libwebem\webem.vcxproj'
    & $msbuildExe $webemProj /p:Configuration=Release /p:Platform=Win32 `
        /p:PlatformToolset=v142 /verbosity:minimal
    if ($LASTEXITCODE -ne 0) { throw "webem.vcxproj build failed (exit $LASTEXITCODE)" }
    Write-Host '=== webem.lib built ==='
}

Write-Host '=== Windows build dependencies ready ==='
