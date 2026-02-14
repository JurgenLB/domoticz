# JsonCpp C4275 Warning Analysis

## Question
Is the C4275 warning error also happening on https://github.com/JurgenLB/jsoncpp.git?

## Answer
**YES**, the error exists in JurgenLB/jsoncpp, but it's currently being **suppressed** rather than properly fixed.

## Detailed Findings

### 1. JurgenLB/jsoncpp Repository
- **URL:** https://github.com/JurgenLB/jsoncpp.git
- **Current State:** Uses suppression approach
- **File:** include/json/value.h
- **Line 61:** `#pragma warning(disable : 4251 4275)` - Suppresses the warning
- **Lines 76, 92, 103:** Exception classes still have `JSON_API` macro:
  ```cpp
  class JSON_API Exception : public std::exception {
  class JSON_API RuntimeError : public Exception {
  class JSON_API LogicError : public Exception {
  ```

### 2. domoticz/jsoncpp Repository  
- **URL:** https://github.com/domoticz/jsoncpp
- **Current State:** No fix for C4275 (only suppresses C4251)
- **File:** include/json/value.h
- **Line 53:** `#pragma warning(disable : 4251)` - Only suppresses C4251
- **Lines 67, 83, 94:** Exception classes have `JSON_API` macro (same issue)

### 3. Our Fix in domoticz Repository
- **Location:** extern/jsoncpp submodule (local changes)
- **Approach:** Proper fix - removed `JSON_API` from exception classes
- **Status:** Fixed locally, not pushed to any upstream jsoncpp repository

## Comparison of Approaches

### Suppression Approach (JurgenLB/jsoncpp current state)
```cpp
#pragma warning(disable : 4275)  // Hides the warning
class JSON_API Exception : public std::exception {  // Still has the issue
```
**Problem:** Just hides the warning, doesn't fix the root cause

### Proper Fix (Our implementation)
```cpp
// No pragma needed
class Exception : public std::exception {  // Removed JSON_API
```
**Benefit:** Fixes the root cause by not exporting exception classes

## Why the Proper Fix is Better

1. **Exception classes don't need DLL exports** - They're caught by value/reference
2. **Follows C++ best practices** - Standard exceptions aren't exported
3. **Eliminates the warning source** - No need for pragmas
4. **No breaking changes** - Code continues to work identically

## Recommendations

1. **For JurgenLB/jsoncpp:** Replace the suppression approach with the proper fix
2. **For domoticz:** Continue using the locally fixed version in the submodule
3. **Alternative:** Submit a PR to JurgenLB/jsoncpp with the proper fix

## Technical Details

### The Warning
- **Code:** C4275
- **Message:** "non dll-interface class 'std::exception' used as base for dll-interface class 'Json::Exception'"
- **Cause:** Exporting a class that inherits from a non-exported base class

### The Fix
Remove `JSON_API` from:
- `Exception` class (base exception)
- `RuntimeError` class (derived exception)
- `LogicError` class (derived exception)

The implementations in `json_value.cpp` remain unchanged - they're compiled and linked but not exported.
