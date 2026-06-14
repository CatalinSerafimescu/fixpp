# Quickstart — Verifying 037 (resend-reply PossDup wire conformance)

The witness is a set of unit cells that parse the **emitted** frame and count field occurrences, plus a golden re-bake and a live re-run. No new test harness type is needed — reuse the existing resend / `build_replay_frame` test fixtures (e.g. the 022 W7 retain test and the 013 resend tests).

## Cell 1 — GapFill carries 43=Y + 122==52 (SC-001, default path)

```
Arrange: drive replay_outbound_range_ (or call build_sequence_reset_gapfill directly with a known
         sending_time) so a GapFill is emitted covering an admin range.
Assert (in order):
  1. parse the emitted frame; assert 35 == "4" AND 123 == "Y"   # honesty: this IS the GapFill
  2. count occurrences of tag 43 == 1 AND value == "Y"
  3. count occurrences of tag 122 == 1
  4. value(122) bytes == value(52) bytes                         # INV-2
  5. tags 8/35/34/49/56/36/123 unchanged vs the pre-037 field set (only 43/122 added)  # INV-6
```

## Cell 2 — Replay dedups 43/122 under the retain knob (SC-002, non-default path)

```
Arrange: SessionConfig.allow_pos_dup = true.
         Construct/store an outbound app frame that ALREADY carries 43=Y and 122=<t> (t != the stored 52,
         to prove the engine value wins).
Assert (in order):
  1. FIRST prove the STORED frame contains 43 AND 122          # honesty (contract C-4): else dedup is untested
  2. replay it via build_replay_frame
  3. count tag 43 in the replayed frame == 1                   # INV-3 / FR-004
  4. count tag 122 in the replayed frame == 1
  5. value(122) bytes == stored value(52) bytes (NOT t)        # INV-2 / FR-005
```

## Cell 3 — Default-path replay byte-identity (SC-003, FR-006)

```
Arrange: SessionConfig.allow_pos_dup = false (default). Store a normal app frame (caller 43/122 stripped on send).
Assert: build_replay_frame output == the pre-037 expected bytes, byte-for-byte.   # INV-4
        (Mechanically: the {9,10,43,122} skip never matches a clean stored frame, so output is unchanged.)
```

## Golden re-bake (SC-001 / SC-003, default-path wire change)

DEFECT 1 changes every fixpp-emitted GapFill. The in-process witness is `tests/interop/happy/hp_fix44_recovery_outbound_answer_test.cpp` (fixpp emits the GapFill, `49=FIXPP_INIT`), golden-compared.

**Required profile switch (the load-bearing golden change):** that test compares the GapFill under the `{52,10}` admin profile (`golden_diff.hpp:54`). The new `122` *equals* `52` (a live wall-clock timestamp) → comparing it verbatim is non-deterministic. Switch the GapFill comparison to the **already-existing `{52,10,122}` profile** (`golden_diff.hpp:60-76`), then re-bake the golden. `43=Y` is deterministic and stays compared verbatim. The synthetic `123`-mutation gate-bite cells (`:95-107`) remain valid.

```
profile:    GapFill golden compare  {52,10}  →  {52,10,122}   (profile already exists in golden_diff.hpp)
re-bake:    hp_fix44_recovery_outbound_answer golden(s); diff must show ONLY the added 43=Y/122 on the 35=4 frame
candidates: also inspect phase-9-harness/golden/*.fix containing a fixpp-emitted 35=4 (sender 49=<our CompID>):
            HP-{QFcpp,QFj}-init-fix44-disconnect-reconnect-noreset, RL-{QFcpp,QFj}-init-fix44-reset-on-logon,
            030 RR-* received-reset / resend goldens; re-bake only the fixpp-emitted ones (received GapFills unchanged).
```

## Live re-run (SC-004)

Re-run the QFcpp + QFJ resend / received-reset interop cells (both roles) and confirm the peers ACCEPT the now-`43=Y` GapFill and the session reaches steady state (no reject, no disconnect). Both engines emit this frame themselves, so acceptance is expected — but it is verified, not assumed.

```
cd phase-9-harness && <run_interop_cell.py for the resend/received-reset cells, both engines, both roles>
expect: green; engine-log-seam golden compare matches the re-baked goldens.
```

## Sanitizers / coverage (Article IX §1)

```
ctest under linux-clang-asan + linux-clang-ubsan over the session + admin-message suites (regression).
Coverage: the two new append_raw blocks (GapFill) + the widened skip predicate (replay) at 100% DA/BRDA.
No alloc gate needed beyond the existing builder gates — the Writer is null_memory_resource-backed
(no per-message heap; [const §XV.1] preserved by construction).
```

## Definition of done

- Cells 1–3 pass; the honesty asserts (C-4) are present and discriminating.
- Re-baked goldens diff shows ONLY the added GapFill 43/122.
- Live QFcpp/QFJ resend cells green, both roles.
- ASan/UBSan green; coverage of changed lines complete.
- No public signature/error/config/codegen/C-ABI change (FR-007) — confirmed by `git diff` of headers (doc-comment only).
