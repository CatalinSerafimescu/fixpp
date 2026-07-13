# Quickstart / Validation: 073 nested-read arena fail-loud

**Feature branch**: `073-nested-read-arena-failloud`

Proves the fail-loud seam end-to-end on both read paths. All witnesses run under the standard debug build (the self-run build gate, [[feedback_self_run_build_gate]]); the sanitizer/coverage matrix runs via `/speckit-verify`.

## Prerequisites

```bash
cd research/G19-fix-fpml-iso20022/library
# configure a debug preset if not present (see MSVC/local build notes); build at -j2 (WSL2 OOM cap)
```

## Faithful exhaustion harness (both witnesses)

Inject a tiny-capacity arena so a genuinely present nested group's sub-`OffsetTable` build fails:

```cpp
std::array<std::byte, kTinyCap> buf;      // sized so build_nested_subview's
std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size(),
                                          std::pmr::null_memory_resource()};
// ... construct the MessageView / C-ABI msg so its parse arena IS `arena` ...
```
`kTinyCap` is chosen (small enough) that the `mr->allocate(sizeof(OffsetTable), …)` inside `build_nested_subview` throws `bad_alloc` → `nullptr` → `alloc_failed`. No hand-built 16 KiB message; no post-hoc flag.

## Scenario 1 — C-ABI fail-loud (SC-001, US1)

```
GIVEN a C-ABI msg with a present nested group and the tiny arena
WHEN  fixpp_group_get_nested_group(...) is called
THEN  it returns FIXPP_ERR_WIRE_LIMIT_EXCEEDED  (NOT FIXPP_ERR_OK / nc=0)
```
Mutation proof: revert the `if (r.alloc_failed) return WIRE_LIMIT_EXCEEDED;` arm → test goes RED (returns OK/nc=0).

## Scenario 2 — Typed fail-loud (SC-002, US2)

```
GIVEN a typed nested read of a present group and the tiny arena
WHEN  msg.<group>()[i].<nestedGroup>() is evaluated
THEN  the returned group_view has .alloc_failed() == true, size()==0, and the process does NOT terminate
```
Mutation proof: stop threading `r.alloc_failed` in the emitter → regenerate → test goes RED (`alloc_failed()` false).

## Scenario 3 — Repeated read (D2 cache-exit discriminator)

```
GIVEN the same exhausted nested group
WHEN  it is read TWICE (second read served from the cached null row)
THEN  BOTH reads signal failure (C-ABI: WIRE_LIMIT_EXCEEDED both times; typed: alloc_failed() both times)
```
Mutation proof: set `alloc_failed` only at the final return (`:768`), not the cache-hit exit (`:748`) → the second read goes RED (silent empty).

## Scenario 4 — Non-failure controls (SC-003, FR-007)

```
GIVEN an ample arena
WHEN  reading (a) an ABSENT nested group and (b) a genuinely count-0 nested group
THEN  neither path raises the failure signal
      C-ABI: (a) TAG_NOT_FOUND, (b) OK/nc=0 ; typed: alloc_failed()==false, size()==0
```

## Scenario 5 — Second-loss: sub-table builds, its `group_slices()` fails (D2 mode (b))

```
GIVEN a cap tuned so the sub-OffsetTable BUILDS (non-null: sizeof(OffsetTable) + ctor fit)
      but its group_slices() reserve/push_back then exhausts the arena (:674 catch)
WHEN  the present nested group is read TWICE
THEN  BOTH reads fail loud
      read 1 → final exit (:768): alloc_failed (typed) / WIRE_LIMIT_EXCEEDED (C-ABI)
      read 2 → cache-hit exit (:748-750), cached non-null row re-materializes → re-throws → same signal
```
Why read twice: a single read pins only the final exit; a fixer who wires `group_slices_status` at `:768`
but forgets to OR it at the cache-hit exit would pass a one-read witness while silently truncating the second read.
Mutation proof: use the D2-null-only formula (`alloc_failed = table == nullptr`) → both reads go RED (report empty
with a non-null table); the OR-with-`group_slices_status().alloc_failed` fix makes both GREEN (fail-loud).

## Regression / parity gate

```bash
# regenerate goldens after the emitter change, then FULL ctest (not narrow — codegen_determinism_test)
codegraph sync   # keep the index fresh after code changes
ctest --preset <debug>            # all wire + capi + codegen tests green; ~20 adapted span callers compile
```
SC-004 (no success/absence behavior change) = the full existing suite green; SC-005 (parity) = scenarios 1+2 both fail-loud.
