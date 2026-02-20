# JWT-CPP C4244 Warning Fix Needed

## Issue Description

The jwt-cpp library generates C4244 warnings on Windows MSVC builds due to type conversions in its internal template code.

### Warning Details

```
C:\projects\domoticz\msbuild\Windows Libraries\include\jwt-cpp\jwt.h(2022,48): 
warning C4244: 'argument': conversion from '_Ty1' to 'const unsigned int', possible loss of data
    with [ _Ty1=__int64 ]
(compiling source file '/webserver/cWebem.cpp')
```

**Location**: jwt.h:2022  
**Context**: Template instantiation in picojson traits (defaults.h:19,58)  
**Issue**: Conversion from `__int64` to `const unsigned int` without explicit cast

## Root Cause

The warning occurs during jwt::verifier template instantiation in the picojson trait system. The jwt-cpp library performs implicit type conversions that MSVC flags as potentially unsafe.

## Required Fix

A pull request should be created in https://github.com/JurgenLB/jwt-cpp.git to address this issue.

### Recommended Solution

Add explicit type casts in jwt.h around line 2022 to eliminate the implicit conversion warning:

```cpp
// Instead of:
some_function(int64_value)

// Use:
some_function(static_cast<unsigned int>(int64_value))
```

Or alternatively, ensure the parameter types match to avoid conversion.

## Windows Build Limitation

**IMPORTANT**: Windows builds on AppVeyor use pre-built libraries that are downloaded during the build process, NOT the git submodules.

### How Windows Build Works

```yaml
# From appveyor.yml:
before_build:
  - appveyor DownloadFile https://raw.githubusercontent.com/domoticz/win32-libraries/master/WindowsLibraries.7z
  - 7z x WindowsLibraries.7z > NUL:
```

This means:
1. **Linux/Mac builds**: Use git submodules in `extern/jwtcpp` (can use JurgenLB fork ✅)
2. **Windows builds**: Use pre-built headers from `WindowsLibraries.7z` (still has old jwt-cpp ❌)

### Why Warning Persists

Even though we updated the submodule to use https://github.com/JurgenLB/jwt-cpp.git, Windows builds still show the C4244 warning because:

1. AppVeyor downloads `WindowsLibraries.7z` from the main domoticz repository
2. This archive contains pre-built jwt-cpp headers with the C4244 issue
3. The Windows build doesn't use the submodule at all
4. Our submodule update to JurgenLB fork only affects Linux/Mac builds

### Current Workaround (TEMPORARY)

We have RE-ADDED pragma directives to suppress the warning for Windows builds:

```cpp
// TEMPORARY: Suppress C4244 warning from jwt-cpp until Windows pre-built libraries are updated
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244)
#endif
#include <jwt-cpp/jwt.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
```

**This is TEMPORARY** and will be removed once the Windows pre-built libraries are updated.

## Path to Permanent Fix

### Step 1: Fix jwt-cpp in Fork
- [ ] Create PR in https://github.com/JurgenLB/jwt-cpp.git
- [ ] Fix C4244 warnings in jwt.h around line 2022
- [ ] Add explicit type casts to eliminate implicit conversion warnings
- [ ] Test on Windows MSVC to verify warnings are gone

### Step 2: Update Windows Pre-Built Libraries
- [ ] After jwt-cpp fork is fixed, rebuild WindowsLibraries.7z
- [ ] Update https://github.com/domoticz/win32-libraries with new version
- [ ] Ensure WindowsLibraries.7z contains fixed jwt-cpp headers

### Step 3: Remove Workaround
- [ ] Remove pragma suppression from cWebem.cpp
- [ ] Remove pragma suppression from NotificationFCM.cpp
- [ ] Verify Windows build has no C4244 warnings
- [ ] Remove this documentation file

## Status

- [x] Updated submodule to JurgenLB fork (helps Linux/Mac builds)
- [x] Documented Windows build limitation
- [x] Added temporary pragma suppression for Windows builds
- [ ] Create PR in https://github.com/JurgenLB/jwt-cpp.git to fix C4244
- [ ] Update Windows pre-built libraries after jwt-cpp is fixed
- [ ] Remove pragma suppression after libraries are updated

## References

- Original jwt-cpp: https://github.com/Thalhammer/jwt-cpp
- Fork with fix needed: https://github.com/JurgenLB/jwt-cpp.git
- Issue location: jwt.h line 2022, picojson traits template instantiation
