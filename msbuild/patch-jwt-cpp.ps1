<#
.SYNOPSIS
    Patches the jwt-cpp traits header in the extern/jwtcpp submodule to fix MSVC
    C4244 narrowing-conversion warnings.

.DESCRIPTION
    The open-source-parsers-jsoncpp traits header typedef's integer_type as
    Json::Value::Int (32-bit) while as_integer() returns val.asInt64() (64-bit),
    causing a C4244 narrowing warning on return.

    The as_date() fix (replacing from_time_t with date(std::chrono::seconds(...)))
    is already present in the main jwt-cpp repository at the submodule commit used
    by this project, so no patch is needed for that.

    The Release|Win32 build includes ../extern/jwtcpp/include before
    ../msbuild/Windows Libraries/include so that the upstream-fixed jwt.h is used.
    This script therefore patches the traits header in extern/jwtcpp.

.PARAMETER JwtCppIncludePath
    Path to the jwt-cpp include directory.
    Defaults to "extern\jwtcpp\include" (relative to the repo root).
#>
param(
    [string]$JwtCppIncludePath = "extern\jwtcpp\include"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Fix: traits.h integer_type -------------------------------------------------
# integer_type is Json::Value::Int (32-bit) but as_integer() returns asInt64()
# (64-bit), causing a C4244 narrowing warning on return.
$traitsPath = "$JwtCppIncludePath\jwt-cpp\traits\open-source-parsers-jsoncpp\traits.h"
if (-not (Test-Path $traitsPath)) {
    Write-Error "File not found: $traitsPath"
    exit 1
}
$content = [System.IO.File]::ReadAllText($traitsPath)
$patched = $content.Replace('using integer_type = Json::Value::Int;', 'using integer_type = Json::Value::Int64;')
if ($patched -eq $content) {
    Write-Warning "No replacement made in $traitsPath - pattern not found (already patched?)"
} else {
    [System.IO.File]::WriteAllText($traitsPath, $patched)
    Write-Host "Patched $traitsPath"
}
