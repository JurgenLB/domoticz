# Instructions for Creating PR to jwt-cpp Fork

This document provides instructions for pushing the MSVC type trait warning fixes to the JurgenLB/jwt-cpp fork and creating a pull request.

## Summary of Changes

The changes add explicit `static_cast<size_t>` to type trait checks in `include/jwt-cpp/jwt.h` to eliminate MSVC C4244 warnings about implicit conversions when `integer_type` (int64_t) is passed to `substr()` which expects `size_t`.

**Commit:** 72772ff  
**Branch:** fix-msvc-type-trait-warnings  
**Files Changed:** include/jwt-cpp/jwt.h

## Changes Made

In `include/jwt-cpp/jwt.h` lines 2508-2517:

```cpp
// Before:
std::declval<string_type>().substr(std::declval<integer_type>())

// After:
std::declval<string_type>().substr(static_cast<size_t>(std::declval<integer_type>()))
```

## Steps to Push and Create PR

### Option 1: Push from the domoticz repository

From the domoticz repository directory:

```bash
cd extern/jwtcpp
git push https://github.com/JurgenLB/jwt-cpp.git fix-msvc-type-trait-warnings:fix-msvc-type-trait-warnings
```

Then go to https://github.com/JurgenLB/jwt-cpp and create a pull request from the `fix-msvc-type-trait-warnings` branch to `master`.

### Option 2: Apply the patch to a fork clone

1. Clone your fork:
```bash
git clone https://github.com/JurgenLB/jwt-cpp.git
cd jwt-cpp
```

2. Create and checkout a new branch:
```bash
git checkout -b fix-msvc-type-trait-warnings
```

3. Apply the patch:
```bash
git apply /path/to/fix-msvc-type-trait-warnings.patch
```

4. Commit and push:
```bash
git add include/jwt-cpp/jwt.h
git commit -m "Fix MSVC C4244 warnings in type trait checks"
git push origin fix-msvc-type-trait-warnings
```

5. Create a pull request on GitHub.

### Option 3: Manually apply changes

Edit `include/jwt-cpp/jwt.h` and make these changes:

**Line 2510-2511:** Change:
```cpp
typename std::is_same<decltype(std::declval<string_type>().substr(std::declval<integer_type>(),
                                                                  std::declval<integer_type>())),
```

To:
```cpp
typename std::is_same<decltype(std::declval<string_type>().substr(static_cast<size_t>(std::declval<integer_type>()),
                                                                  static_cast<size_t>(std::declval<integer_type>()))),
```

**Line 2516:** Change:
```cpp
typename std::is_same<decltype(std::declval<string_type>().substr(std::declval<integer_type>())),
```

To:
```cpp
typename std::is_same<decltype(std::declval<string_type>().substr(static_cast<size_t>(std::declval<integer_type>()))),
```

## Pull Request Description

Use this for the PR description:

---

**Title:** Fix MSVC C4244 warnings in type trait checks

**Description:**

This PR adds explicit `static_cast<size_t>` to `integer_type` parameters in type trait checks to eliminate MSVC C4244 implicit conversion warnings.

### Problem

MSVC reports C4244 warnings when compiling code that uses jwt-cpp due to implicit conversions in SFINAE type trait checks:

```
warning C4244: 'argument': conversion from '_Ty1' to 'const unsigned int', possible loss of data
with [ _Ty1=__int64 ]
```

These warnings occur in the `is_substr_start_end_index_signature` and `is_substr_start_index_signature` type traits when `integer_type` (int64_t) is implicitly converted to `size_t` for `substr()` calls during template instantiation.

### Solution

Add explicit `static_cast<size_t>` conversions in the type trait declarations. This eliminates the warnings without affecting functionality since these are compile-time type checks only.

### Changes

- `include/jwt-cpp/jwt.h` lines 2510, 2511, 2516: Add `static_cast<size_t>` to substr parameter type checks

### Testing

- Compiles without warnings on MSVC
- No behavioral changes (SFINAE checks only)
- All existing tests pass

---

## After the PR is merged

Once the PR is merged into the JurgenLB/jwt-cpp fork:

1. Update the domoticz submodule reference to point to the new commit
2. The CI builds should then work without warnings
