# Quickstart — 008-message-store (Phase 1)

**Branch:** `008-message-store` | **Plan:** [plan.md](plan.md)

Build / test / sanitizer / coverage / verify recipes. Every command runs with `cwd` inside the library submodule: `cd research/G19-fix-fpml-iso20022/library` from the parent root.

---

## 0. Pre-`/implement` probes (run BEFORE `/speckit-implement`)

```bash
# Probe std::crc32 availability for FileStore CRC32 (research D-3). Decides
# whether T002 adds the crc32c/1.1.2 Conan row.
cat > /tmp/probe_crc32.cpp <<'EOF'
#include <cstdint>
#include <ranges>
int main() {
#ifdef __cpp_lib_std_crc32   // SD-6 feature-test (none exists today)
    return 0;
#else
    return 1;                // expected fall-through on Clang 22 / libc++ / libstdc++
#endif
}
EOF
clang++ -std=c++23 /tmp/probe_crc32.cpp -o /tmp/probe_crc32 && /tmp/probe_crc32 && echo "std::crc32 available" || echo "std::crc32 NOT available — add crc32c/1.1.2 Conan row at T002"

# Verify [2d §4.5] / [2d §4.7] cross-doc amendments shipped at 007 merge
# (research D-8). Should return the three load-bearing lines unchanged.
grep -n 'store_factory\|flush_for_session_close' \
    include/fixpp/session/session_config.hpp \
    .specify/2d-threading.md | head -10

# Verify the structural cross-doc edits this feature OWES (FR-037/038/039)
# are still at the documented anchors (so the edit is a single-line touch).
sed -n '76p'   spec/coverage-index.md         # FR-037 anchor
sed -n '240p'  spec/feature-catalogue.md      # FR-038 OSS-002
sed -n '332p'  spec/feature-catalogue.md      # FR-038 COM-009
sed -n '598p'  .specify/architecture.md       # FR-039
```

---

## 1. Build (local pre-PR gate, `[const §XVII.7]`)

```bash
# AskUserQuestion before any local build per [const §XVII.7] resource gate.
# Local toolchain target: Clang 22 ([const §XVII.7]).

# Tier-1 debug build (the required pre-PR confirmation):
conan install . --profile=conan/profiles/linux-clang-debug --build=missing
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug -j
ctest --preset linux-clang-debug --output-on-failure

# Release build (catches optimised-only failures, HALO firing):
conan install . --profile=conan/profiles/linux-clang-release --build=missing
cmake --preset linux-clang-release && cmake --build --preset linux-clang-release -j

# PR-description line to include after Tier-1 debug succeeds:
#   local build: green on linux-clang-debug @ <git-sha>
```

---

## 2. Test (GoogleTest; the 21 `[2e §9]` seams)

The 21 seams enumerated in `[2e §9]` map to 21 test files under `tests/`. The seam names below match the design-doc ordinals (referenced by name not ordinal — `[2e §9]` paragraph 1).

```bash
# Run all 008 seams (Tier-1 debug):
ctest --preset linux-clang-debug --output-on-failure -R 'memory_store|file_store|store_|retrieve_|quickfix_compat'

# Per-seam test targets:
#   seam 1  test_memory_store_round_trip
#   seam 2  test_file_store_crash_survival       (Linux Tier-1 + Windows Tier-2)
#   seam 3  test_file_store_torn_write           (Linux Tier-1 + Windows Tier-2)
#   seam 4  test_memory_store_capacity
#   seam 5  test_store_fifo_fair                 (TSan + ASan)
#   seam 6  test_store_cancellation_contract     (per-method)
#   seam 7  test_outbound_store_post_commit      (Writer::commit byte-equality)
#   seam 8  test_retrieve_with_gaps              (invalid input + gap detection)
#   seam 9  test_retrieve_visitor                (awaitable visitor + span lifetime; TSan)
#   seam 10 test_store_reset                     (atomic-rename crash test, 3 cuts)
#   seam 11 test_quickfix_compat_path_b_guard    (compile-time static_assert)
#   seam 12 test_quickfix_compat_cfg_loader      (config translation)
#   seam 13 bench_memory_store                   (latency — see §6)
#   seam 14 test_store_alloc_guard               (mallocnesia + tools/check_alloc.py — see §4)
#   seam 15 test_memory_store_zero_allocator_calls  (tracking-PMR counter)
#   seam 16 test_store_pmr_poison_retrieve       (trap_throw via [2a §4.2])
#   seam 17 test_store_corpus_replay             (tests/conformance/; [const §VII.5] store-side)
#   seam 18 test_store_shutdown_ordering         (TSan + ASan)
#   seam 19 test_file_store_flush_for_session_close   (graceful vs terminal close)
#   seam 20 test_store_seqnum_out_of_order
#   seam 21 fuzz_message_store                   (libFuzzer — see §5)
```

---

## 3. Sanitizers (Tier 1, `[const §IX.2]`)

```bash
# ASan
conan install . --profile=conan/profiles/linux-clang-asan --build=missing
cmake --preset linux-clang-asan && cmake --build --preset linux-clang-asan -j
ctest --preset linux-clang-asan --output-on-failure

# UBSan
conan install . --profile=conan/profiles/linux-clang-ubsan --build=missing
cmake --preset linux-clang-ubsan && cmake --build --preset linux-clang-ubsan -j
ctest --preset linux-clang-ubsan --output-on-failure

# TSan (MANDATORY — 2e is threading-touching; seams 5/6/9/18/19/20/21)
conan install . --profile=conan/profiles/linux-clang-tsan --build=missing
cmake --preset linux-clang-tsan && cmake --build --preset linux-clang-tsan -j
ctest --preset linux-clang-tsan --output-on-failure -R 'fifo_fair|cancellation_contract|retrieve_visitor|shutdown_ordering|flush_for_session_close|seqnum_out_of_order'
```

**TSan note** — before suppressing any race that appears to live inside asio's internals, check our own wiring first per `feedback_tsan_asio_internal_check_impl_first`. A second `codex:codex-rescue` opinion on a suspected asio-internal race is cheap insurance against silently violating I-01 / I-17.

---

## 4. Allocation guards (`tools/check_alloc.py` + `mallocnesia`, Linux/Clang only)

```bash
# Symbol scan — must report 0 global new/delete in fixpp::session::MemoryStore::store
# and fixpp::session::FileStore::store after parse → fromApp (seam 14):
python3 tools/check_alloc.py \
    --binary build/linux-clang-release/tests/session/test_store_alloc_guard \
    --module fixpp::session::MemoryStore::store \
    --module fixpp::session::FileStore::store

# mallocnesia interceptor — 10⁴-message session run, expect 0 global-heap
# new/delete/malloc between parse and fromApp; PMR-arena allocations are
# expected and NOT flagged. Path per reference_mallocnesia_path memory.
LD_PRELOAD=tools/mallocnesia/libmallocnesia.so \
    build/linux-clang-release/tests/session/test_store_alloc_guard --gtest_filter='*Mallocnesia*'

# Seam 15 — MemoryStore::store zero-allocator-calls under bounded policy.
# Tracking PMR resource counter unchanged after 10⁴ store() calls.
ctest --preset linux-clang-debug -R memory_store_zero_allocator_calls -V
```

---

## 5. libFuzzer fuzz (seam 21; voluntary per `[2e §9 seam 21]` Gate-A discretion)

```bash
# Build the libFuzzer harness:
conan install . --profile=conan/profiles/linux-clang-asan --build=missing
cmake --preset linux-clang-asan -DFIXPP_BUILD_FUZZ=ON
cmake --build --preset linux-clang-asan --target fuzz_message_store -j

# Ten-minute corpus run per [const §VII.7]-extended:
build/linux-clang-asan/tests/fuzz/fuzz_message_store \
    tests/fuzz/corpus/message_store/ \
    -max_total_time=600 -timeout=10 -rss_limit_mb=1024

# Also runs under UBSan + TSan invariants per [const §IX.4]-extended:
# (separate build presets — fuzz_message_store target lives in each).
```

---

## 6. Bench (`[const §VIII.1]` / `[const §VIII.2]`)

Latency ceilings per `[2e §6.6]` enforced by Google Benchmark. CI fails on:

- > **5%** regression vs the previous tagged release for `MemoryStore::*` rows;
- > **2×** regression for `FileStore::*` rows (disk-bound floor; bench-soft per the per-host hardware baseline at `bench/baselines/session/`).

```bash
# MemoryStore latency (seam 13):
conan install . --profile=conan/profiles/linux-clang-release --build=missing
cmake --preset linux-clang-release && cmake --build --preset linux-clang-release -j bench_memory_store
build/linux-clang-release/bench/session/bench_memory_store \
    --benchmark_out=bench/results/session/bench_memory_store_$(git rev-parse HEAD).json \
    --benchmark_out_format=json

# Compare against baseline:
python3 bench/scripts/compare_baseline.py \
    --baseline bench/baselines/session/bench_memory_store.json \
    --candidate bench/results/session/bench_memory_store_*.json \
    --threshold 0.05  # 5% for MemoryStore

# FileStore disk-bound rows:
build/linux-clang-release/bench/session/bench_file_store \
    --benchmark_out=bench/results/session/bench_file_store_$(git rev-parse HEAD).json \
    --benchmark_out_format=json
python3 bench/scripts/compare_baseline.py \
    --baseline bench/baselines/session/bench_file_store.json \
    --candidate bench/results/session/bench_file_store_*.json \
    --threshold 1.0  # > 2× FAIL (1.0 = 100% slowdown)
```

---

## 7. Coverage (`[const §IX.1]` ≥95% line / ≥85% branch on touched modules)

```bash
conan install . --profile=conan/profiles/linux-clang-coverage --build=missing
cmake --preset linux-clang-coverage && cmake --build --preset linux-clang-coverage -j
ctest --preset linux-clang-coverage --output-on-failure

# IMPORTANT — Regenerate per-binary .profraw FRESH; never reuse a prior /
# aborted build's profraw (per feedback_coverage_profraw_staleness:
# mismatched profraw silently zeroes functions, producing false-RED
# catastrophic-low coverage). Run feature binaries fresh, awk-sum per-file
# (TOTAL is global), first bin positional not -object.

# Per-file gate basis is lcov DA / BRDA (per feedback_coverage_gate_lcov_basis):
llvm-profdata merge build/linux-clang-coverage/*.profraw \
    -o build/coverage/008.profdata
llvm-cov export build/linux-clang-coverage/tests/session/test_* \
    -instr-profile=build/coverage/008.profdata \
    -format=lcov > build/coverage/008.lcov.info

# Touched modules ≥95% line / ≥85% branch:
python3 tools/coverage/check_lcov_threshold.py \
    --lcov build/coverage/008.lcov.info \
    --threshold-line 0.95 --threshold-branch 0.85 \
    --include 'include/fixpp/session/.*\.hpp$' \
    --include 'src/session/.*\.cpp$'
```

**Uncovered lines / branches discipline** (per `[const §IX.1]`): every uncovered line / branch carries the Opus risk-assessment paired evidence in `.specify/decisions/008-message-store-verify.md` (genuine error / edge path → tested; defensive / unreachable / trivial-accessor → waived with one-line rationale; recorded-non-assessable-touch → exempt by inspection). Per-file lcov DA / BRDA is the binding gate; the Codecov/patch external soft gate is recorded as an explicit waiver-with-rationale per `feedback_codecov_patch_vs_lcov_da_brda_gate` (precedent: PR #73 / PR #74).

---

## 8. `/speckit-verify` (mandatory post-`/implement`, `[const §XVII.8]`)

```bash
# Tier-1 mirror; serial preset matrix per [const §XVII.8]:
/speckit-verify 008-message-store

# Writes the decision record:
#   .specify/decisions/008-message-store-verify.md
# Verdict: GREEN (all PASS or SKIPPED-with-reason) /
#          YELLOW (every FAIL paired with --waive=<task-id>:<rationale>) /
#          RED (at least one unwaived FAIL).
#
# /gate-b refuses to start the Codex review loop if the verify record is
# absent or RED. YELLOW is accepted but carries waiver context forward.
```

---

## 9. `/gate-a` (Phase-4 bundle Gate A — NOT the Phase-2 design-doc Gate A)

The Phase-2 design-doc Gate A converged via `/gate-a-ph2` (rounds 1–3 + post-cap line-edit pass; convergence log at `.specify/2e-msgstore.md` Appendix C). This bundle's Phase-4 Gate A is its own review of record per `[const §XVII.1]`.

```bash
# Run after /speckit-plan and BEFORE /speckit-tasks. Gate A blockers must be
# resolved or waived before /tasks runs per [const §XVII.1].
# Order per the bundle-local pipeline statement in plan.md Constitution-Check
# row [const §XVI.4]:
#   /plan → Gate A → /tasks → /analyze → /implement → /simplify → /speckit-verify → Gate B
/gate-a 008-message-store

# Both Codex passes per feedback_gate_a_codex_dual_pass:
# (1) codex:codex-rescue, (2) /codex:adversarial-review.
# Opus judge overrules Codex by reading source per
# feedback_gate_a_structural_rec_and_judge_independence.
```

---

## 10. `/gate-b` (PR review before merge)

```bash
# Mandatory pre-merge per [const §XVII.2]. Independence per [const §XVII.3].
# Hard precondition: /speckit-verify non-RED AND feature-completeness audit
# non-failing ([const §XVII §8]).
/gate-b <PR-number>

# /gate-b fixer MUST re-run tools/check_layers.py + architecture.md
# cross-check when adding new headers (per feedback_gate_b_check_layers_post_fixer).
# For 2e: every new include/fixpp/session/*.hpp MUST place its declarations
# in the fixpp::session namespace AND respect the [arch §2.3] core/ leaf
# rule (no back-edge into session/).
```

---

## 11. Windows Tier-2 (manual / nightly per `[const §II.3]`)

```bash
# On the Tier-2 Windows MSVC runner, triggered by the `windows` PR label or
# nightly schedule per [const §IX.6]:
conan install . --profile=conan/profiles/windows-msvc-debug --build=missing
cmake --preset windows-msvc-debug && cmake --build --preset windows-msvc-debug -j

# Seams that have Windows-specific code paths:
#   seam 2  test_file_store_crash_survival       (FlushFileBuffers, LockFileEx)
#   seam 3  test_file_store_torn_write           (SetEndOfFile)
#   seam 10 test_store_reset                     (MoveFileExW + MOVEFILE_WRITE_THROUGH)
#   seam 19 test_file_store_flush_for_session_close
ctest --preset windows-msvc-debug -R 'crash_survival|torn_write|store_reset|flush_for_session_close' -V
```

UBSan-on-MSVC is N/A per `[const §IX.3]`; equivalent UB coverage is Linux/Clang Tier 1.

---

## 12. Codegen freshness (not directly applicable here, but recorded)

008 does NOT regenerate codegen (no `Reify.hpp` consumed; no new typed-message namespace). But per `project_codegen_emitter_staleness`, if any out-of-line `.cpp` in this feature consumes 003's `Reify.hpp` indirectly through `dict::version_registry` and a debug pass fails under asan / ubsan / tsan / coverage: build `fixpp-codegen` → `rm -r build/*/_codegen` → reconfigure. (Recorded as a safety net; 2e is dictionary-agnostic per `[2c §1.1]` so this should not fire.)

---

**Next:** `/speckit-tasks` consumes `plan.md` + `research.md` + `data-model.md` + `contracts/` and emits `tasks.md` with dependency-ordered T-IDs (T001..T-N). Then `/speckit-analyze` runs the drift check. Then `/gate-a`. Then `/speckit-implement`.
