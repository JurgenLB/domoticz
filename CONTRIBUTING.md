# Contributing to Domoticz

Thank you for your interest in contributing! Please read the following guidelines before submitting a pull request.

## Branch strategy

All changes should be based on the **`development`** branch:

- **Bug fixes** – create a feature branch from `development`
- **New features** – please discuss on the [forum](https://forum.domoticz.com/) first, then branch from `development`

## Building from source

See [INSTALL.md](INSTALL.md) and the [wiki](https://wiki.domoticz.com/Build_Domoticz_from_source) for full instructions.

Quick start (Ubuntu/Debian):

```bash
sudo apt-get install make gcc g++ libssl-dev git libcurl4-gnutls-dev \
  libusb-dev libmosquitto-dev python3-dev zlib1g-dev liblua5.3-dev \
  uthash-dev libsqlite3-dev python3-pytest python3-pytest-bdd

cmake -DCMAKE_BUILD_TYPE=Release CMakeLists.txt
make
```

## Git submodules

Domoticz uses several bundled libraries as git submodules under `extern/`:

| Submodule | Path | `branch` in .gitmodules |
|-----------|------|------------------------|
| jsoncpp | `extern/jsoncpp` | master |
| minizip | `extern/minizip` | *(not set)* |
| jwt-cpp | `extern/jwtcpp` | master |
| sqlite-amalgamation | `extern/sqlite-amalgamation` | *(not set)* |
| libwebem | `extern/libwebem` | master |

> **Important:** Never include the `extern/` folder in a pull request. Submodule contents are managed externally through git submodule references.

### Per-branch submodule strategy

The project uses a **different submodule update strategy depending on the branch**:

| Branch | Command | Behaviour |
|--------|---------|-----------|
| `master` (stable) | `git submodule update --init --recursive` | Checks out the **exact commit SHA** pinned in the repository index. Guarantees reproducible, stable builds. |
| `development` | `git submodule update --init --remote` | Fetches the **latest commit on the upstream tracking branch** (`branch =` in `.gitmodules`). Keeps bundled libraries up-to-date during active development. |

CMake applies this automatically (see `CMakeLists.txt`). CI workflows for `development` apply it as well.

When working on a feature branch based on `development`, use `git submodule update --init --recursive` to stay on the pinned SHAs until you intentionally want to bump a dependency.

### Understanding the three submodule update commands

When working with submodules, three variants of `git submodule update` are commonly used. Understanding their differences helps you choose the right one:

#### 1. `git submodule update --init --recursive`

```bash
git submodule update --init --recursive
```

- **Initializes** any submodule that has not yet been set up locally (`--init`).
- **Checks out the exact commit SHA** that is recorded in the parent repository's git index — the pinned version chosen by the last committer.
- **Recurses into nested submodules** (`--recursive`), so submodules-within-submodules are also updated.
- **Use this** when building Domoticz from source for the first time, or after pulling new commits, to ensure your submodule state exactly matches what the repository expects.
- This is the command run automatically by `cmake` during the build (see `CMakeLists.txt`).

#### 2. `git submodule update --init --remote`

```bash
git submodule update --init --remote
```

- Ignores the pinned commit SHA stored in the parent repository.
- Instead, **fetches and checks out the latest commit from the remote tracking branch** (the branch specified by `branch = ...` in `.gitmodules`, or `master`/`main` if not set).
- **Use this** only when you intentionally want to advance all submodules to the tip of their upstream branches — for example, when bumping a submodule to a newer version before committing the updated SHA to the parent repo.
- After running this command you must `git add extern/<name>` and commit the new submodule SHA into the parent repository.
- **Do not use** this during a regular build; it breaks reproducibility by pulling in unreleased upstream changes.

#### 3. `git submodule update --init --recursive --force`

```bash
git submodule update --init --recursive --force
```

- Does everything `--recursive` does, **plus** it forcibly discards any local uncommitted modifications inside the submodule working trees before checking out the pinned commit.
- Equivalent to running `git checkout --force` inside each submodule.
- **Use this** when a submodule working tree has been accidentally modified and you need to restore the exact state recorded in the parent repository.
- Useful in CI pipelines where a dirty submodule state from a previous run could otherwise cause conflicts.

#### Summary

| Command | Checkout target | Discards local changes | Recurses |
|---------|----------------|----------------------|---------|
| `--init --recursive` | Pinned commit SHA in parent repo | No | Yes |
| `--init --remote` | Latest commit on tracked remote branch | No | No (by default) |
| `--init --recursive --force` | Pinned commit SHA in parent repo | **Yes** | Yes |

For day-to-day development and all CI builds, use `--init --recursive` (or let CMake do it for you). On the `development` branch, CMake and the CI workflows automatically use `--remote` to track the latest upstream. Use `--force` to clean up an accidentally-modified submodule.

## Code style

C++ code must follow the clang-format rules in `clang-format.txt`:

- **C++17** standard
- **Tabs** for indentation (8-space width)
- Braces on a new line for classes, functions, and control statements
- Column limit: 200 characters

## Pull request checklist

- [ ] Branch is based on `development`
- [ ] `extern/` folder is **not** included in the PR
- [ ] Code follows the project style (`clang-format.txt`)
- [ ] Changes compile without warnings
- [ ] Existing tests still pass
