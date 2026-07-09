# Quickstart: verify the 066 dictionary-backed inbound parse

All commands run with cwd inside the library submodule
(`research/G19-fix-fpml-iso20022/library`).

## 1. Reproduce the defect through REAL session dispatch (pre-fix, RED)

The critical methodology point: prove it on the SHIPPED path, not a `Parser{dict}` unit parse. Write a session-level witness that:
1. Constructs a `Session` (or the C-ABI engine) with a real FIX44 dictionary.
2. Drives a group-bearing frame through inbound dispatch (`on_inbound_frame` / the C-ABI receive path) — e.g. an `ExecutionReport` with `NoLegs(555)` carrying leg members followed by a trailing outer field.
3. In the application callback, reads the last group instance and queries the trailing outer field.

On the pre-fix dict-free parse this returns `FIXPP_ERR_OK` + a wrong value (extent runs to end-of-message). The witness asserts `TAG_NOT_FOUND` → **RED**. This RED-first proof is mandatory (`[const Art VII §3]`); the existing `Parser<Index>{dict}` tests are already green and cannot demonstrate the shipped-path defect.

## 2. Apply the fix, verify GREEN

After the change (session `inbound_tv_` built at `open()`; `parse_and_dispatch_` uses `Parser{*inbound_tv_}`):

```bash
cmake --preset linux-clang-debug
cmake --build build/linux-clang-debug -j2
ctest --test-dir build/linux-clang-debug -R 'session|capi|interop' --output-on-failure
```

Expect: the new real-dispatch witnesses PASS (trailing field `TAG_NOT_FOUND` on the last instance; scalar-as-group `TYPE_MISMATCH`), and the existing session/interop/C-ABI suites remain green (each intended behavior delta an explicit, reviewed edit — no silent breakage).

## 3. Clone / reify identity (FR-007)

Witness: clone an inbound dict-backed message (`fixpp_msg_clone`) and read the same group from the clone — assert the clone's result equals the source's (membership-bounded), not a positional divergence. Same for a `reify` owning handle.

## 4. No new global heap; arena fit (SC-004)

```bash
ctest --test-dir build/linux-clang-debug -R 'alloc' --output-on-failure
```

Confirm no new global-heap allocation on the inbound parse+read path (the `table_view` is built once at `open()`; per-message membership + nested sub-views come from the per-message stack arena). Arena fit is **witnessed** (SC-004 / FR-009), because dict-backed nested reads build sub-`OffsetTable`s from the stack arena — a NEW cost the dict-free path never incurred, landing on BOTH arenas:
- a representative group-bearing **app** message parses+reads within `kInboundParseArena` (16 KiB) — no heap fallback, successful read;
- a group-bearing **admin** message parses+reads within the tighter `kAdminParseArena` (8 KiB) — no heap fallback, successful read;
- a **near-cap / headroom probe** (a message approaching the arena bound still succeeds);
- a **pathological deeply-nested** message **fails closed** within the group-depth (`kMaxGroupDepth=16`) / entry caps and the arena — never over-read or corrupt (FR-009).

## 5. C-ABI freeze unchanged (SC-003)

```bash
ctest --test-dir build/linux-clang-debug -R 'abi|freeze' --output-on-failure
```

No delta in the exported-symbol golden / `capi_freeze.sha256` — the change is behavioral.

## 6. Sanitizers (Tier-1 mirror — /speckit-verify)

`/speckit-verify 066-dict-backed-inbound-parse` runs the ASan/UBSan/TSan matrix over the session + C-ABI paths, validating the `inbound_tv_` stable-address binding, the clone-owned `table_view` lifetime, and the reify view lifetime.

## 7. Prerequisite check for 065 (SC-005)

After 066 lands, re-plan 065 and run its real-dispatch C-ABI nested-read witness (issue #179 layout, `555 → 604 ×2 → 687`): RED before 066+065, GREEN after — confirming the dictionary-backed root is the precondition that makes the C-ABI cursor fix actually repair the shipped path.
