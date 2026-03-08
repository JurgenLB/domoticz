#!/bin/sh
# build_windows.sh – Domoticz Windows build helper
#
# Equivalent of the AppVeyor CI steps defined in msbuild/appveyor.yml.
# Run this script from the repository root using Git Bash or MSYS2 on Windows.
#
# Usage: ./msbuild/build_windows.sh [Release|Debug]
#
# Prerequisites (must be on PATH):
#   git, nuget, curl, 7z, msbuild, ISCC (InnoSetup)

set -e

CONFIGURATION="${1:-Release}"
PLATFORM="x86"
BUILD_FOLDER="$(cd "$(dirname "$0")/.." && pwd)"

cd "$BUILD_FOLDER"

echo "=== Domoticz Windows Build ==="
echo "    Configuration : $CONFIGURATION"
echo "    Platform      : $PLATFORM"
echo "    Build folder  : $BUILD_FOLDER"
echo ""

# ---------------------------------------------------------------------------
# before_build
# ---------------------------------------------------------------------------

echo "[1/5] Updating submodules..."
git submodule update --init --remote extern/jwtcpp
git submodule update --init extern/libwebem

echo "[2/5] Installing InnoSetup via NuGet..."
nuget install Tools.InnoSetup

echo "[3/5] Downloading Windows Libraries..."
cd msbuild
curl -fL -o WindowsLibraries.7z \
    https://raw.githubusercontent.com/domoticz/win32-libraries/master/WindowsLibraries.7z
7z x WindowsLibraries.7z > /dev/null
cd "$BUILD_FOLDER"

echo "[4/5] Injecting Directory.Build.props for libwebem..."
cat > extern/libwebem/Directory.Build.props <<EOF
<Project>
  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalIncludeDirectories>$BUILD_FOLDER\\extern\\jwtcpp\\include;$BUILD_FOLDER\\msbuild\\Windows Libraries\\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
EOF

# ---------------------------------------------------------------------------
# build
# ---------------------------------------------------------------------------

echo "[5/5] Building Domoticz with MSBuild..."
msbuild msbuild/domoticz.sln \
    /m \
    /p:Configuration="$CONFIGURATION" \
    /p:Platform="$PLATFORM" \
    /verbosity:minimal

# ---------------------------------------------------------------------------
# after_build
# ---------------------------------------------------------------------------

echo "Packaging..."
cp appversion.h version_windows_x86.h
cp History.txt  history_windows_x86.txt

# Locate InnoSetup (nuget places it in a versioned sub-directory)
INNO_DIR="$(ls -d Tools.InnoSetup.*/ 2>/dev/null | head -1)"
if [ -z "$INNO_DIR" ]; then
    echo "Error: Tools.InnoSetup directory not found." >&2
    exit 1
fi
mv "$INNO_DIR" Tools.InnoSetup

Tools.InnoSetup/tools/ISCC msbuild/WindowsInstaller/DomoticzSetup.iss
msbuild msbuild/package.proj

# ---------------------------------------------------------------------------
# artifacts summary
# ---------------------------------------------------------------------------

echo ""
echo "=== Build complete. Artifacts ==="
for f in domoticz_windows_x86.zip version_windows_x86.h history_windows_x86.txt History.txt; do
    [ -f "$f" ] && echo "  $f" || echo "  $f (not found)"
done
