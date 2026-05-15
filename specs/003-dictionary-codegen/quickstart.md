---
id: 003-dictionary-codegen
title: Quickstart — build / codegen / test / bench / verify / gate
spec_kit_step: /plan Phase 1
last_updated: 2026-05-15
---

# Quickstart — 003-dictionary-codegen

> **Status (Gate A round 1, 2026-05-15): bundle NOT converged — `blocked_on_replan`.** The commands below are the intended workflow but are **not** runnable as specified until RC#1 (`version_profile`/`resolve_application_version`/`field_traits` are 003-owned new surface, not 002-merged), RC#2 (inherited 2c §4.1.3/§4.7 decimal-decoding defect — 2c reopen required), and RC#3 (open `dict/`→`wire/` layer amendment) are resolved at re-`/plan`. See `plan.md` `## Gate A`. `/tasks` is gated.

All commands run with cwd inside the library submodule
(`research/G19-fix-fpml-iso20022/library`). The agent surfaces an
`AskUserQuestion` before any local Conan/CMake build per `[const §XVII.7]`
resource gate; the commands below are the reference, not auto-run.

## 1. Configure (runs codegen)

```bash
conan install . -pr conan/profiles/linux-clang-debug --build=missing
cmake --preset linux-clang-debug          # configure-time: fixpp::dict::generate-vXX
```

- The configure step builds the host tool `tools/codegen/fixpp-codegen`
  (C++23, links the merged `fixpp::dict` from 002 — F1 Candidate A) and runs
  it once per codegen version. Output lands **only** under
  `build/linux-clang-debug/_codegen/include/fixpp/{v42,v44,v50sp2,vt11}/...`
  and `.../_dispatch/...` — never the source tree (AC-T2 / AC-C4).
- No new Conan row: `pugixml/1.14` is reused transitively from 002.

## 2. Build + test

```bash
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug -L codegen      # codegen-emitter + shape tests
ctest --preset linux-clang-debug -L dictionary   # reify / dispatch / registry
ctest --preset linux-clang-debug -L integration  # multi-version / FIXT cross-vocab
```

Key targets: `codegen_conformance_test` (seam #1, CI subset), `reify_test`,
`reify_dispatch_test` (seam #15a/b/c), `reify_move_test` (seam #14),
`flyweight_shape_test` (seam #18), `length_data_table_test` (seam #19),
`determinism_test` (NFR-003-7 vs the 4 golden headers),
`version_registry_test` (AC-X*).

## 3. Tier-1 preset matrix (`[const §IX.6]`; research.md D-17)

| Preset | What runs |
|---|---|
| `linux-clang-debug` | every test target; `tools/check_layers.py` |
| `linux-clang-release` | every test except TSan-only; the 3 bench bars |
| `linux-clang-asan` / `-ubsan` | every test target |
| `linux-clang-tsan` | `reify_cross_strand_test` (AC-R5 / seam #12) |
| `linux-clang-coverage` | ≥90 % line / ≥80 % branch on `tools/codegen/*`, `dict/reify*`, `dict/version_registry*`, vendored wire contract |
| `linux-gcc-release` | sanity build (tool + generated headers under GCC) |

Tier-2 (`windows-msvc-*`): manual / nightly. No C-ABI surface (spec §5) → no
abidiff golden (`[const §IX.5]` N/A).

## 4. Bench (release; ±5 % vs `bench/baselines/`)

```bash
ctest --preset linux-clang-release -L bench
```

- `bench/codegen/typed_accessor_bench` — string/char ≤20 ns, decimal ≤75 ns,
  `field_value` ≤25 ns (NFR-003-1).
- `bench/codegen/compile_time_bench` — single-version ≤3 s (load-bearing),
  all-versions ≤15 s soft (`FIXPP_BENCH_ALL_VERSIONS_CEILING`) (NFR-003-2).
- `bench/dictionary/reify_bench` — `reify_as` 20-tag ≤1 µs / 200-tag ≤10 µs;
  `reify` ≤1.2 µs; codegen-table lookup arm (NFR-003-3; seam #5/#6).

Baselines are written on the first green CI run that includes each bench.

## 5. Determinism check (NFR-003-7 / AC-T1 / AC-T2)

```bash
ctest --preset linux-clang-debug -R determinism_test
git status --porcelain   # MUST be empty for the source tree after configure
```

`determinism_test` regenerates each version's `Messages.hpp` and asserts
byte-identical output vs the 4 goldens under
`specs/003-dictionary-codegen/contracts/golden/`. Those goldens are generated
codegen output, **checked in at `/implement`** (one `tasks.md` row) — the
`contracts/golden/` directory does **not** exist in the bundle at Gate A.
Regenerating a golden is a deliberate, Gate-A-reviewed step on any
codegen-template change.

## 6. /speckit-verify (mandatory after /speckit-implement, `[const §XVII.8]`)

```bash
/speckit-verify 003-dictionary-codegen
```

Produces `.specify/decisions/003-dictionary-codegen-verify.md`. Verdict
`GREEN` (all PASS/SKIPPED-with-reason) is required for `gate-b-done`;
`YELLOW` (every FAIL paired with `--waive=<task-id>:<rationale>`) for
`gate-b-waived`. Serial preset matrix — never parallel.

## 7. Gate A / Gate B

- **Gate A** (after this plan; before `/tasks`): both Codex passes
  (`/codex:rescue` + `/codex:adversarial-review`) per
  `feedback_gate_a_codex_dual_pass.md`, then Opus post-judging. F1 and R6
  explicitly flagged for review (plan.md Gate A §). Record:
  `.specify/decisions/003-dictionary-codegen-gatea.md`.
- **Gate B** (post-`/implement`, pre-merge): `/gate-b` — Codex hostile review
  → Opus triage → Sonnet fixer → flag-commit → Codex fixer. Independence per
  `[const §XVII.3]`. Precondition: a non-`RED` `/speckit-verify` record.
  Label applied via `gh api` REST `--repo CatalinSerafimescu/fixpp`
  (auto-memory `project_gate_label_application`).

## 8. Codegen tool — direct invocation (debugging)

```bash
./build/linux-clang-debug/tools/codegen/fixpp-codegen \
    --xml dictionaries/FIX44.xml --version v44 \
    --out build/linux-clang-debug/_codegen/include/fixpp
```

Deterministic: same XML in → byte-identical headers out (re-run and diff).
The tool links `fixpp::dict`; it parses the XML through 002's already-fuzzed
`XmlLoader::load()` — one XML truth (research.md D-1).
