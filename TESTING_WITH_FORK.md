# Testing with JurgenLB jwt-cpp Fork

This document explains how to test this PR with the JurgenLB fork of jwt-cpp without including the submodule change in the PR.

## Why?

The PR needs to use the standard Thalhammer/jwt-cpp repository, but for testing purposes, you may want to use the JurgenLB fork which contains specific fixes or changes.

## Setup for Local Testing

### Option 1: Temporarily Change Submodule URL

```bash
# Change the submodule URL to the fork
git config submodule.extern/jwtcpp.url https://github.com/JurgenLB/jwt-cpp.git

# Update the submodule
git submodule sync
git submodule update --init --recursive

# Your .gitmodules file remains unchanged, only local git config is modified
```

### Option 2: Manual .gitmodules Edit (Not Recommended for PR)

If you manually edit `.gitmodules` to point to the fork, remember to:

1. **DO NOT commit** the .gitmodules change
2. Add it to .git/info/exclude temporarily:
   ```bash
   echo ".gitmodules" >> .git/info/exclude
   ```
3. Revert before committing:
   ```bash
   git checkout -- .gitmodules
   ```

## Reverting to Original

To switch back to the original Thalhammer/jwt-cpp:

```bash
# Remove the local config override
git config --unset submodule.extern/jwtcpp.url

# Update to use the URL from .gitmodules
git submodule sync
git submodule update --init --recursive
```

## For CI/CD

CI/CD systems will use the URL specified in the committed `.gitmodules` file, which points to the original Thalhammer/jwt-cpp repository. This ensures the PR doesn't change external dependencies.

## Current Status

- **Committed .gitmodules**: Points to `https://github.com/Thalhammer/jwt-cpp`
- **For testing**: Use the commands above to temporarily use `https://github.com/JurgenLB/jwt-cpp.git`
- **PR includes**: NO submodule URL changes
