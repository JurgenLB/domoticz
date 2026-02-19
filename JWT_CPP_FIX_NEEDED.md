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

## Current Workaround

Previously, we used pragma directives to suppress the warning:

```cpp
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244)
#endif
#include <jwt-cpp/jwt.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
```

However, this has been removed in favor of fixing the underlying issue in jwt-cpp itself.

## Status

- [ ] Create PR in https://github.com/JurgenLB/jwt-cpp.git
- [ ] Fix C4244 warnings in jwt.h
- [ ] Update domoticz to use fixed version
- [ ] Remove this documentation file

## References

- Original jwt-cpp: https://github.com/Thalhammer/jwt-cpp
- Fork with fix needed: https://github.com/JurgenLB/jwt-cpp.git
- Issue location: jwt.h line 2022, picojson traits template instantiation
