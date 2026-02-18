# Testing with JurgenLB jwt-cpp Fork

This document explains the jwt-cpp submodule configuration for this PR.

## Current Configuration

This PR uses the JurgenLB fork of jwt-cpp for testing purposes.

**Committed .gitmodules**: Points to `https://github.com/JurgenLB/jwt-cpp.git`

## Why the Fork?

The JurgenLB fork contains specific fixes or changes needed for testing this PR. Both local development and CI/CD systems will use this fork.

## Setup

To initialize the submodule with the fork:

```bash
# Initialize and update submodules
git submodule update --init --recursive
```

This will automatically use the JurgenLB fork as specified in `.gitmodules`.

## Using the Original Repository (If Needed)

If you need to temporarily test with the original Thalhammer/jwt-cpp repository:

```bash
# Override locally to use original repo
git config submodule.extern/jwtcpp.url https://github.com/Thalhammer/jwt-cpp

# Update the submodule
git submodule sync
git submodule update --init --recursive
```

To revert back to the fork:

```bash
# Remove the local override
git config --unset submodule.extern/jwtcpp.url

# Update to use the URL from .gitmodules (the fork)
git submodule sync
git submodule update --init --recursive
```

## For CI/CD

CI/CD systems will use the URL specified in the committed `.gitmodules` file, which now points to `https://github.com/JurgenLB/jwt-cpp.git` for testing purposes.

## Current Status

- **Committed .gitmodules**: Points to `https://github.com/JurgenLB/jwt-cpp.git` ✅
- **CI/CD testing**: Uses the JurgenLB fork ✅
- **Local development**: Uses the JurgenLB fork by default ✅
