# Wire Bench Report — 004-wire-codec T048/T052

## Latency measurements (linux-clang-debug, NDEBUG off, 2026-05-16)

Debug timings are ~10–25x release; the [2b §6.6] ceilings apply to linux-clang-release+warm-cache.
The ±5% regression gate (CI) fires on linux-clang-release.

| Bench | Ceiling [2b §6.6] | Debug measured | Ceiling sourced? |
|---|---|---|---|
| BM_Framer_Feed_NoCarry | ≤ 30 ns | ~749 ns | YES — from [2b §6.6] table row 1 |
| BM_Parser_Iter_20tag | ≤ 80 ns | ~27 ns | YES — from [2b §6.6] table row 2 |
| BM_Parser_Index_20tag | ≤ 400 ns | ~5877 ns | YES — from [2b §6.6] table row 3 |
| BM_Parser_Index_200tag | ≤ 4 µs | ~43295 ns | YES — from [2b §6.6] table row 4 |
| BM_OffsetTable_Find_32slot | ≤ 15 ns | ~34 ns | YES — from [2b §6.6] table row 5 |
| BM_Writer_Commit_20tag | ≤ 80 ns | ~1856 ns | YES — from [2b §6.6] table row 7 |
| BM_Writer_Commit_200tag | ≤ 800 ns | ~17500 ns | YES — from [2b §6.6] table row 8 |
| Validator::validate | ≤ 200 ns | see "Validator::validate — wired (075 Gate B r1)" below | YES — now wired |

## D-8 — check_alive() cost (T052, linux-clang-debug)

| Bench | Total 200 calls (ns) | Per-access (ns) |
|---|---|---|
| BM_CheckAlive_200tag (debug, real slot lookup) | 1148.9 | ~5.74 |
| BM_Bytes_200tag (baseline, no generation check) | 852.5 | ~4.26 |
| Ratio | **1.35×** | |

Verdict: 1.35× < 2× threshold → per-N-access sampling fallback NOT triggered.
Release cost: analytically zero (constexpr empty body).

## D-14 — hffix comparison

hffix comparison NOT wired — D-14 says measured-not-blocker.
hffix is absent from the Conan package graph. Wire in hffix before v1.0 RC
per [const §VIII.4] and add BM_HffixParser_20tag to parser_bench.cpp.

## Validator::validate — wired (075 Gate B r1, 2026-07-15)

`bench/wire/validator_bench.cpp` un-pauses the US4-deferred validator bench
(Article VIII §3: "No perf change merged without a benchmark in the same
PR" — 075-live-wire-enum-validation made `table_view::enum_valid()` do real
per-field work on the `dictionary_driven_validator::validate()` hot path).
Built under `linux-clang-release`; uses a REAL `fixpp::dict::table_view`
loaded from the shipped `dictionaries/FIX44.xml` via `dict::XmlLoader` ->
`Dictionary::as_table_view()` (not `support/mock_dict_table.hpp`, which
carries no enum store). Each fixture is pre-framed + pre-parsed ONCE outside
the timed loop; only `validate()` itself is timed. Baseline:
`bench/baselines/wire/validator_bench.json`.

| Bench | Fixture | Ceiling [2b §6.6] | Measured release (mean, 10 reps) |
|---|---|---|---|
| BM_Validator_Validate_Heartbeat | admin Heartbeat(35=0), header only | ≤ 200 ns | **568 ns** |
| BM_Validator_Validate_Logon | admin Logon(35=A), EncryptMethod(98) enum | ≤ 200 ns | **848 ns** |
| BM_Validator_Validate_NewOrderSingle | FIX44 D, Side/OrdType/TimeInForce enums | ≤ 200 ns | **1214 ns** |
| BM_Validator_Validate_NewOrderSingleMultiValueExecInst | FIX44 D + ExecInst(18)="1 G 6" (multi-value) | ≤ 200 ns | **1265 ns** |

**FINDING — TRIAGED (Gate B PR #193, 2026-07-15).** All four cases exceed the
`[2b §6.6]` ≤200 ns ceiling. Isolation (rebuilding this same bench with
`enum_valid()` reverted to `return true`) splits the cause in two:

| Case | pre-075 (`enum_valid→true`) | 075 (real) | 075 delta |
|---|---|---|---|
| Heartbeat | 489 ns | 568 ns | +79 ns (+16%) |
| Logon | 741 ns | 848 ns | +107 ns (+14%) |
| NewOrderSingle | 910 ns | 1214 ns | +304 ns (+33%) |
| NOS + multi-value | 986 ns | 1265 ns | +279 ns (+28%) |

1. **The ceiling breach is PRE-EXISTING** — `validate()` is 489–986 ns (2.4–4.9×
   over 200 ns) *with enum-checking off*; the Heartbeat fixture (zero enum-backed
   body fields) proves it. Not introduced by 075.
2. **075 adds a real +16%–+33%** on the enum path (default-off; only
   `validate_inbound_messages=true` sessions pay it).

**075 is NOT blocked**: Article VIII §3 (bench must exist) is satisfied by this
harness; §2 (±5% vs baseline) has no prior validator baseline to regress against
— this SEEDS it; §4's ≤200 ns is a v1.0 *target* and the breach is pre-existing.
**User disposition (2026-07-15):** accept + merge; optimize BOTH the pre-existing
walk and the enum delta toward 200 ns as a scheduled v1.0 step (after Orchestra) —
`research/G19-fix-fpml-iso20022/remaining-work/validator-perf-optimization.md`.

## PR68-08 — Release baseline follow-up (gate-b/r1, 2026-05-17)

Triage finding `opus_pr68_1_triage.md` PR68-08 (P2, waived): the [2b §6.6] ±5%
regression gate is explicitly CI/linux-clang-release-scoped; this REPORT is honest
that all timings above are debug-only and do NOT claim release compliance.

**Tracked follow-up:** when `linux-clang-release` CI lands (currently blocked by
`CMAKE_MAKE_PROGRAM`/Ninja preset gap in the GCC-release profile), re-run the
bench suite under `linux-clang-release`, record the release baselines here, and
commit the baseline JSON `_note` as "release-baseline committed". The ±5%
regression gate fires on linux-clang-release CI; this follow-up closes the
PR68-08 waiver.

The `Validator::validate` ceiling (≤ 200 ns, [2b §6.6] table row for validator)
is no longer debug-only deferred — see "Validator::validate — wired (075 Gate B
r1)" above for the `linux-clang-release` baseline and the ceiling-breach finding.
