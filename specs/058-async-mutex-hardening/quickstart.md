# Quickstart — async_mutex hardening (Cluster-4)

All commands run from the library submodule root:
`cd research/G19-fix-fpml-iso20022/library`.

## Build & run the async_mutex suite (local, resource-gated)

Local builds are resource-heavy and require explicit user approval (Article XVII §7). The Tier-1
mirror is `/speckit-verify` (serial preset matrix). Ad-hoc:

```bash
# configure + build the sync tests on the debug preset (after conan install)
cmake --build build/linux-clang-debug --target test_async_mutex_layout_golden -j2
ctest --test-dir build/linux-clang-debug -R '^sync_' --output-on-failure
```

## The deterministic AM-P1 witness (the discriminating one)

`test_async_mutex_aba_interleave` is built with `-DFIXPP_ASYNC_MUTEX_TEST_SEAM` (its target ONLY; the
library and every other test build seam-free). It installs a hook that pins one thread between the
free-link load and the pop CAS while another thread runs pop-pop-push of the same head, landing the
ABA every run.

- **Against pre-fix code:** RED (the ABA lands → slot returned twice / parked record clobbered).
- **After D-1 (generation-tagged head):** GREEN (the stale CAS fails and retries).

Mutation check (SC-007): revert D-1 → this test must go RED. Repeat per-fix for every witness.

## Sanitizer matrix

```bash
ctest --test-dir build/linux-clang-tsan  -R '^sync_' --output-on-failure   # ASan/UBSan analogous
```

NOTE (FR-013): AM-P1 is **TSan-invisible** (all-atomic cycle). TSan-green is necessary but NOT
sufficient for AM-P1 — the deterministic seam witness + the generation-bump reasoning argument carry
its correctness.

## Coverage (100%-reachable-branch DoD — SC-004)

Measured on the coverage lane, lcov **BRDA/DA** basis (NOT `llvm-cov report` aggregate). Every
unreachable branch of `async_mutex.hpp` is waived with a written proof in
`.specify/decisions/058-async-mutex-hardening-verify.md`. The branch inventory + waivers are
enumerated at the coverage-design gate (after `/tasks`).

## What NOT to touch

- Public `async_mutex` API, the `131120`/`16` layout golden, the `no-std-mutex` gate,
  `test_async_mutex_layout_golden` — all preserved (FR-011/FR-012).
- No consumer call-site (MemoryStore/FileStore/SeqnumManager/Session) changes — the fixes are additive
  safety inside the header.
