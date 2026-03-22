<#
.SYNOPSIS
    Patches jwt-cpp headers in the downloaded Windows Libraries to fix MSVC C4244
    narrowing-conversion warnings.

.DESCRIPTION
    The Windows Libraries package ships an older jwt-cpp version with two type
    mismatches that MSVC reports as C4244 errors when compiling cWebem.cpp:

      1. traits.h  - integer_type is typedef'd to Json::Value::Int (32-bit) while
                     as_integer() returns val.asInt64() (64-bit).

      2. jwt.h     - as_date() uses system_clock::from_time_t(std::round(...)),
                     where std::round returns double and from_time_t expects time_t
                     (implicit double->time_t narrowing). Replaced with
                     date(std::chrono::seconds(std::llround(...))) which avoids
                     any implicit conversion.

    This script applies minimal, targeted text replacements to correct both issues
    after the archive has been extracted.

.PARAMETER WindowsLibrariesPath
    Path to the extracted "Windows Libraries" directory.
    Defaults to "msbuild\Windows Libraries" (relative to the repo root).
#>
param(
    [string]$WindowsLibrariesPath = "msbuild\Windows Libraries"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# --- Fix 1: traits.h ---------------------------------------------------------
# integer_type was Json::Value::Int (32-bit) but as_integer() returns asInt64()
# (64-bit), causing a C4244 narrowing warning on return.
$traitsPath = "$WindowsLibrariesPath\include\jwt-cpp\traits\open-source-parsers-jsoncpp\traits.h"
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

# --- Fix 2: jwt.h as_date() --------------------------------------------------
# Old code used system_clock::from_time_t(std::round(as_number())) which has an
# implicit double->time_t conversion (C4244).  Replace the two return statements
# with date(std::chrono::seconds(...)) which avoids any narrowing:
#   - std::llround returns long long
#   - std::chrono::seconds takes long long
#   - as_integer() returns Int64 (long long after Fix 1 above)
$jwtPath = "$WindowsLibrariesPath\include\jwt-cpp\jwt.h"
if (-not (Test-Path $jwtPath)) {
    Write-Error "File not found: $jwtPath"
    exit 1
}
$content = [System.IO.File]::ReadAllText($jwtPath)
$oldDateImpl = "date as_date() const {`n`t`t`tusing std::chrono::system_clock;`n`t`t`tif (get_type() == json::type::number) return system_clock::from_time_t(std::round(as_number()));`n`t`t`treturn system_clock::from_time_t(as_integer());`n`t`t}"
$newDateImpl = "date as_date() const {`n`t`t`tusing std::chrono::system_clock;`n`t`t`tif (get_type() == json::type::number)`n`t`t`t`treturn date(std::chrono::seconds(std::llround(as_number())));`n`t`t`treturn date(std::chrono::seconds(as_integer()));`n`t`t}"
$patched = $content.Replace($oldDateImpl, $newDateImpl)
if ($patched -eq $content) {
    Write-Warning "No replacement made in $jwtPath - pattern not found (already patched?)"
} else {
    [System.IO.File]::WriteAllText($jwtPath, $patched)
    Write-Host "Patched $jwtPath"
}
