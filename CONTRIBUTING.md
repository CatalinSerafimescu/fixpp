# Contributing to fixpp

> **Note:** This is a work-in-progress sandbox project (see README disclaimer).
> Contributions are welcome subject to the constitution (`.specify/constitution.md`) and the
> Codex review gate process (`.specify/architecture.md` + `.specify/codex-review.md`).

## Toolchain

- **Compiler: Clang 22** (constitution Article II §2, Conan profiles in `conan/profiles/linux-clang-*`).
  - Local: install via your distro or `apt.llvm.org`'s `llvm.sh 22 all`.
  - CI provisions Clang 22 the same way, so local == CI.
- **Build: CMake ≥ 3.28 + Ninja.**
- **Deps: Conan 2.x.** Profiles live in `conan/profiles/`.
- **Python: 3.12, SWIG >=4.2,<4.5, pytest** (only when working on Python bindings). The
  upper bound is a temporary #296 safety cap (an intermittent 3.11 GC segfault
  under SWIG 4.5.0, not yet root-caused); it applies to both the wheel build
  (`bindings/python/pyproject.toml`) and the direct CMake build
  (`bindings/python/CMakeLists.txt`).

## Pre-PR build gate (mandatory)

Per constitution Article XVII §7, **every PR must come with a confirmed-green local build before it is opened.** No "open the PR to see what CI says" — CI verifies green local work, it does not replace it.

Minimum local cycle before opening a PR:

```bash
# 1. Conan install (linux-clang-debug at minimum)
conan install . -pr conan/profiles/linux-clang-debug --build=missing \
  -of build/linux-clang-debug

# 2. Configure + build + test
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug --output-on-failure

# 3. Python bindings (only if your change touches bindings/python/)
cmake --preset linux-clang-debug -DFIXPP_BUILD_PYTHON=ON \
  -B build/linux-clang-debug-py
cmake --build build/linux-clang-debug-py
PYTHONPATH=build/linux-clang-debug-py/lib pytest bindings/python/tests/ -v
```

Then add this line to the PR description:

```
local build: green on linux-clang-debug @ <git-sha>
```

PRs missing that line, or with a known-red local build, are rejected at review.

### AI agents: ask before running local builds

Local Conan + CMake + build + sanitizer cycles are resource-heavy (CPU, disk, time). When an AI agent (Sonnet, Opus, Codex) needs to run a local build on the user's machine, it **must surface an `AskUserQuestion` first** stating:

- which preset(s) it wants to build,
- approximate expected runtime,
- whether sanitizers will rebuild from scratch.

The agent does NOT auto-run `conan install` / `cmake --build` / `ctest` without explicit user approval. After the build completes, the agent reports the result (green/red, failures if any) and either proceeds to PR-open (green) or fixes the issue and re-asks (red).

## Quick setup (pre-commit hooks)

```bash
# 1. Install pre-commit
pip install pre-commit

# 2. Install hooks (runs on every commit automatically)
pre-commit install

# 3. (optional) Run all hooks manually now
pre-commit run --all-files
```

## Slow / manual hooks

Some hooks are marked `stages: [manual]` because they are too slow for every
commit:

```bash
# Run clang-tidy on all C++ files (requires a configured build)
pre-commit run --hook-stage manual clang-tidy

# Run CMake configure sanity check
pre-commit run --hook-stage manual cmake-configure
```

## All available presets

`CMakePresets.json` ships these configure+build+test presets. Sanitizer/coverage presets rebuild from scratch (slow):

| Preset | Use |
|---|---|
| `linux-clang-debug` | Day-to-day dev; minimum for the pre-PR gate above. |
| `linux-clang-release` | Release build sanity. |
| `linux-clang-asan` | AddressSanitizer build (slow). |
| `linux-clang-ubsan` | UndefinedBehaviorSanitizer build (slow). |
| `linux-clang-tsan` | ThreadSanitizer build (slow). |
| `linux-clang-coverage` | llvm-cov instrumented build (slow). |
| `linux-gcc-release` | GCC sanity. |
| `windows-msvc-{debug,release,asan}` | Windows; runs in Tier 2 CI on demand. |

CI runs all of the above on every PR (Tier 1) except the Windows presets, which run only when the PR carries the `windows` label or via manual Actions dispatch.

## Codex review gates

Every non-trivial design change requires **Gate A** (pre-implementation);
every PR requires **Gate B** (post-implementation). See
`.specify/codex-review.md` for the invocation procedure.
