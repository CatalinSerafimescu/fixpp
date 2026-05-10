# Contributing to fixpp

> **Note:** This is a work-in-progress sandbox project (see README disclaimer).
> Contributions are welcome subject to the constitution (`constitution.md`) and the
> Codex review gate process (`architecture.md` + `.specify/codex-review.md`).

## Quick setup

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

## Build

```bash
# Install Conan dependencies
conan install . -pr conan/profiles/linux-clang-debug --build=missing \
  -of build/linux-clang-debug

# Configure + build
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug

# Run tests
ctest --preset linux-clang-debug --output-on-failure
```

See `CMakePresets.json` for all available presets (sanitizers, coverage, Windows).

## Codex review gates

Every non-trivial design change requires **Gate A** (pre-implementation);
every PR requires **Gate B** (post-implementation). See
`.specify/codex-review.md` for the invocation procedure.
