# Quickstart: fixpp#201 required-presence scoping — validation guide

How to prove the feature end-to-end. Assumes the library submodule cwd and the standard local build (clang-debug, `-j2` per the WSL2 build-resource cap).

## Prerequisites

- Configured build dir (bypass the conan preset collision if needed):
  `cmake -S . -B build/debug -DCMAKE_TOOLCHAIN_FILE=<conan-toolchain> -DCMAKE_BUILD_TYPE=Debug -G Ninja`
- For the QuickFIX parity golden **regeneration only**: quickfix-cpp 1.16.0 at the parent `reference-engines/` and `-DFIXPP_BUILD_QUICKFIX_GOLDEN=ON`. CI and normal runs consume the checked-in golden and need neither.

## 1. Group-scope fix (US1) — conforming message accepted

```
ctest --test-dir build/debug -R required_scope_test -V
```
Expect: FIX44 PositionReport (AP) required set excludes 732/733; FIX50SP2 TradeCaptureReport (AE) excludes 54; control FIX44 NewOrderSingle (D) excludes Symbol(55). See [contracts Contract 4](./contracts/census-and-agreement.md).

## 2. Per-instance group required check (US2)

```
ctest --test-dir build/debug -R validator_type_check_test -V
```
Expect: a group instance missing a required member → rejected with the offending tag; complete instances accepted.

## 3. Non-circular census (US4) — the correctness proof

```
ctest --test-dir build/debug -R required_scope_census -V
```
Expect: exact set-equality across all 10 dicts (runtime + IR projection). To confirm it is not a tautology, temporarily revert the `in_group` gate in `expand_field_list` and re-run → the census MUST go RED (RED-proof obligation, [contracts Contract 1](./contracts/census-and-agreement.md)).

## 4. QuickFIX parity (US4) — the tiebreaker (9 dicts)

Consume the checked-in golden (no QuickFIX link):
```
ctest --test-dir build/debug -R required_scope_parity -V
```
To regenerate the golden after a dictionary change (local only):
```
cmake -S . -B build/qfgolden -DFIXPP_BUILD_QUICKFIX_GOLDEN=ON <...> && cmake --build build/qfgolden --target quickfix_required_golden -j2
```
Expect: QuickFIX `DataDictionary` required set == census oracle, per message, 9 QuickFIX dicts (no vlatest row).

## 5. Two-tier agreement (US3)

```
ctest --test-dir build/debug -R required_scope_two_tier -V
```
Expect: runtime and generated typed `validate_<Msg>` return identical verdicts for the affected frames — confirming no codegen change was needed.

## 6. Regression floor (no read-golden / ABI drift)

```
ctest --test-dir build/debug -R 'codegen_determinism|dictionary_pure|wire_pure|wire_dict' -j2
```
Expect: all green; v44/v42/vt11/v50sp2/vlatest read goldens byte-identical; no C-ABI change.

## Success = all six green, with step 3's census proven RED on `in_group` revert.
