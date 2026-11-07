# Research: 100 wire group-read integrity

Code read at `fa778a8e` (worktree `fixpp-parallel`). Line numbers are leads at that commit; re-locate by symbol.

## Survey (what is there today)

| Surface | Today | Where |
|---|---|---|
| Probe cap | `kMaxBuildProbe = 128`; on reach, `skip_insert = true`, `status_` untouched | `OffsetTable::build`, `src/wire/offset_table.cpp` (the `// DoS bound: leave this occ un-indexed` arm) |
| Other build failures | `status_ = err_…(); entries_.clear(); return;` (entry cap: `err_offset_table_full()`) | same function |
| Parse | `Parser::parse` returns the build status's error | `include/fixpp/wire/parser.hpp`, the `build_status()` check |
| Inbound late parse failure | `dispatch_outcome::parse_failed` → `close_on_late_parse_failure_()`: terminal close, no Reject, no Logout, no callback (092 C-6) | `src/session/session.cpp` |
| Seed | one random seed per process; test hook `set_overlay_seed_for_testing` | `offset_table.cpp`, `tests/support/wire_test_hooks.hpp` |
| Top-level group read | `group_slices(no_tag)` = `group_slices_status(no_tag).slices`, flag dropped | `offset_table.cpp` |
| Typed top-level | `MessageView::group<>()` builds `group_view{table_.group_slices(NoTag), ctx}` (flag defaults false); generated top-level accessors only delegate to it | `parser.hpp`; `tools/codegen/fixpp-codegen/emit_messages.cpp` |
| Typed nested | generated accessor threads `nested_group_slices(...).alloc_failed` into `group_view` (073) | `emit_messages.cpp` |
| Group decision | count tag absent → absent; count last → `{first, 0}`; else next field must be a member of `no_tag` in context, else absent. Count value never read | `OffsetTable::group()` |
| Validator's group decision | `group_first_field(msg_type, parent_path, tag) != 0` (context form, bare fallback on a context miss); `declared_count == 0` short-circuits | `include/fixpp/wire/validator.hpp`, `validate_group_level` / `consume_group` |
| Delimiter hook | `dict_hooks::group_delim_fn()(opaque_dict, ctx, no_tag)` → `table_view::group_first_field(msg_type, parent_path, no_tag)`: 0 when not a group | `include/fixpp/wire/dict_hooks.hpp`; `parser.hpp` `for_table_view` |
| Flat C getter | `find` absent → `TAG_NOT_FOUND`; empty slices → `TYPE_MISMATCH` (scalar, zero count, count last, exhaustion, group too large alike); shell `new_object` with no catch | `src/capi/message_read.cpp`, `fixpp_msg_get_group` |
| Nested C getter | `alloc_failed` → `WIRE_LIMIT_EXCEEDED`; empty → membership-free presence probe: absent → `TAG_NOT_FOUND`, present → `OK`/0 (a scalar too); shell `new_object` with no catch | `fixpp_group_get_nested_group` |
| C-ABI docs | CA-010 block lists `TAG_NOT_FOUND`, `TYPE_MISMATCH`, `INDEX_OUT_OF_RANGE`; `WIRE_LIMIT_EXCEEDED` undocumented for the getters | `include/fix/c_api/message.h` |
| C-ABI version | 1.14 (099, BREAKING) | `include/fix/c_api/version.h` |
| Cache row | `group_span{data, count, no_tag}`, `sizeof <= 16` asserted; a non-group caches as a count-0 row | `include/fixpp/wire/offset_table.hpp` |

Existing cells that pin today's defects (they flip, each superseded by name in tasks):
- `HostileInputHardening.CraftedCollisionSetSkipsTargetUnderMatchingSeed` (`tests/wire/hostile_input_hardening_test.cpp`): build OK + target absent → FR-008.
- `CapiInboundFrameDispositionsQ33.MsgGetGroupAtHeadroomExhaustion` (`tests/capi/inbound_frame_dispositions_capi_test.cpp`): `TYPE_MISMATCH` on exhaustion → FR-005/FR-007.

Cells that must stay green (controls): `CraftedCollisionSetDefeatedByDifferentSeed`; `MessageReadGroup.NestedGroupEmptyGroupCountLastField`; `MessageReadFailloud.Control_CountZeroNestedGroup_AmpleArena_ReturnsOkZero`; `MessageReadGroup.NonGroupTagReturnsTypeMismatch`; `dict066_scalar_as_group_test`; `MessageReadGroup.DictFreeGroupReadReportsTypeMismatch` (#220); `InboundFrameDispositionsQ33.GroupSlicesAtHeadroomExhaustion` (calls the span wrapper, which is kept); B-004-7's witnesses.

QuickFIX reference for #647 (ruling note, parent `research/reviews/ruling_B81_647_zero_count_group_v1.md`): C++ `2ce8a60` `Message::setGroup` decides with `DataDictionary::getGroup`; J `4969564` `Message.parseGroup` with `dd.getGroup` and sets the count field first. Both: count field set, group count 0.

## Decisions

### D1 — Probe cap ends the build (#645)

**Decision:** in the probe-cap arm, `status_ = err_offset_table_full(); entries_.clear(); overlay_.clear(); return;`, the shape of every other build-failure arm. The constant and the bound stay.

**Rationale:** one ending a caller already handles: `Parser::parse` fails with `wire_offset_table_full`, the C ABI maps it to `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` (`src/capi/error.cpp`), an inbound session takes 092's late-parse-failure terminal close. The frame needs a crafted collision set against a per-process random seed, so a terminal close is proportionate. No new `core::error` slot, so no `expected_error_map.csv` row.

**Alternatives:**
- A new `wire_index_probe_cap` error: more precise in a log, but a new slot, a CSV row and a C-ABI code mapping for an ending the caller handles identically. Rejected.
- Linear scan on a miss, or re-seeding and rebuilding: the first is the O(occ²) the cap prevents; the second is unbounded retries against an adaptive attacker. Rejected.

### D2 — Group-ness of a zero-count group comes from the delimiter oracle (#647)

**Decision:** in `OffsetTable::group()`, after locating the count entry and before today's next-field membership check: if the declared count (`parse_declared_count`) is 0 **and** `hooks_.group_delim_fn()` is set **and** it answers non-zero for `(stored context, no_tag)`, return `group_index{no_tag, first, 0}` (present, empty). Otherwise the existing path runs unchanged. `consume_group_extent` needs no change: a nested zero-count count field is a member of the outer group, and `consume_one` already advances one entry past it when the next entry is not a nested member, so the outer instance's extent continues; the nested read itself is a sub-table `group()` call and gets D2's rule there.

**Rationale:**
- The delimiter oracle is the same question the validator asks (`group_first_field(msg_type, parent_path, tag) != 0`), so the read and the validator agree on what is a group (SC-004), and a scalar holding `0` stays not-a-group (the oracle answers 0 for it).
- Taking the branch only for a zero count leaves every non-zero frame on today's path (FR-009), including hand-built hook tables that answer members-yes / delimiter-0 (B-384-2).
- The "count last" shortcut (`first >= entries_.size()`) returns present today without asking the dictionary; it is left as it is (its readers already treat it as present), and its flat-getter answer is fixed by D4.

**Alternatives:**
- `table_view::group_first_field_exact` (no bare fallback on a context miss): stricter than the validator, so a frame the validator accepts could read not-a-group. Not reachable through `dict_hooks` without a new hook. Rejected for agreement with the validator; recorded for Gate A.
- Decide by the value 0 alone: a scalar `0` becomes a phantom group. Rejected (ruling note).

### D3 — The group read result says whether the tag is a group (#647, FR-003)

**Decision:** `group_slices_result` and `nested_slices_result` gain `bool is_group` (true when `group()` succeeded, including present-and-empty). The cache row `group_span` gains the same flag (it has 2 bytes of padding; the `sizeof <= 16` assert holds, to be confirmed by the assert itself). The span wrapper `group_slices()` stays as is.

**Rationale:** the C getters must tell "zero-count group" (OK/0) from "scalar" (`TYPE_MISMATCH`), and both read an empty span today. The flag is a by-value bool on trivially copyable structs: no allocation, no new call.

**Alternatives:** call `group()` again from the getter (a second O(n) scan per read, and two places to keep in agreement). Rejected.

### D4 — C-ABI getters: one table of endings (#541, #644, #647)

**Decision:** both getters answer, in this order:

| Condition | Flat `fixpp_msg_get_group` | Nested `fixpp_group_get_nested_group` |
|---|---|---|
| tag absent (message / instance) | `TAG_NOT_FOUND` | `TAG_NOT_FOUND` |
| any allocation failed (slices, sub-table, sub-build OOM, cursor shell) or a group wire limit (D5) | `WIRE_LIMIT_EXCEEDED` | `WIRE_LIMIT_EXCEEDED` |
| present, not a group in this context | `TYPE_MISMATCH` | `TYPE_MISMATCH` (was `OK`/0) |
| group, zero instances | `OK`, count 0, `*out` null (was `TYPE_MISMATCH`) | `OK`, count 0, `*out` null |
| group, n instances | `OK`, count n, cursor | `OK`, count n, cursor |

The cursor-shell `new_object` sits in `try { … } catch (std::bad_alloc const&) { return FIXPP_ERR_WIRE_LIMIT_EXCEEDED; }` with the out-parameters left null/0. The order puts the presence probe first for the flat getter (as today; its lookup allocates nothing) and the failure check before the presence probe for the nested getter (as 073 ruled). They differ in one case, kept from 073: on an exhausted arena an absent **nested** tag answers `WIRE_LIMIT_EXCEEDED`, because the nested sub-table is built over the instance before the tag is looked up.

**Rationale:** `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` ("Wire-level capacity or size limit exceeded (DoS bound)") is what the nested getter returns today and what `wire_offset_table_full` / `wire_group_too_large` already map to. One code for one cause (FR-007).

**Alternatives:** `FIXPP_ERR_OUT_OF_MEMORY`-style code: the arena is a fixed per-message bound, not process memory; and the nested getter's shipped answer would change too. Rejected.

### D5 — A group read stopped by a wire limit is not an empty group (found at plan)

**Finding:** `OffsetTable::group()` returns `err_group_too_large` when an instance exceeds `max_group_entries_per_instance` (default 4096) or nesting exceeds 16. `group_slices_status` turns that into an empty, not-failed result, so the typed read is empty and the flat getter answers `TYPE_MISMATCH`. A peer reaches the instance cap with a frame that repeats a member tag past 4096 in one instance (a 65536-byte frame holds that), on a validation-off session. Same class as #644 (a present group read as empty); not filed.

**Decision (proposed, for Gate A):** the result's failure flag covers it: `group_slices_result::alloc_failed` is renamed in meaning to "the read did not complete" — set for `bad_alloc` and for `wire_group_too_large` — and the C getters answer `WIRE_LIMIT_EXCEEDED` (D4). The typed `group_view::alloc_failed()` keeps its name (public API) and its doc comment widens to "the read did not complete (allocation, or a group wire limit)".

**Alternatives:** leave it out and file an issue (keeps one silent-loss ending this feature exists to remove, on the same lines); a second flag `limit_exceeded` (two flags that every reader must OR). The first is the fallback if Gate A rules the scope too wide.

### D6 — Typed top-level read reports the flag (#644)

**Decision:** `MessageView::group<>()` calls `group_slices_status(NoTag)` and constructs `group_view{r.slices, ctx, r.alloc_failed}`. The generated top-level accessors delegate to `group<>()`, so no generated text changes and no SHA pin moves.

### D7 — A nested sub-table whose build failed for a wire limit is a failed read (found at plan)

**Decision:** `resolve_nested_result`'s mode (c) widens from "sub-build `out_of_memory`" to "sub-build failed with `out_of_memory` or `wire_offset_table_full`", so D1's new failure inside a nested sub-table is reported, not read as an empty nested group. Malformed-data degradation (other errors) stays not-failed, as 073 scoped it.

### D8 — C-ABI version (FR-010)

**Decision:** C-ABI **1.15, BREAKING** (`[const §X.7]`): the nested getter's scalar answer changes (`OK` → `TYPE_MISMATCH`) and the flat getter's exhaustion answer changes (`TYPE_MISMATCH` → `WIRE_LIMIT_EXCEEDED`). No symbol, struct or code is added. `include/fix/c_api/message.h`'s CA-010 block and the two functions' docs list the table in D4. The version history comment in `version.h` names this feature.

### D9 — Witnesses (SC-001)

- #645: a crafted collision frame under `set_overlay_seed_for_testing` (the existing `colliding_tags` / `make_collision_frame` helpers): parse fails with `wire_offset_table_full`; the existing cell flips; its different-seed sibling stays a control. Plus an inbound-session cell: the frame closes the session with no Reject and no callback (reuse 092's `LateSite_*` rig if it can feed a real collision frame under the seed hook; otherwise a parser-level cell plus the existing late-site cells, stated in tasks).
- #644: a top-level group whose slice allocation fails (the Q33 null-upstream headroom rig): typed `alloc_failed()` true; flat getter `WIRE_LIMIT_EXCEEDED`; the Q33 flat cell flips.
- #541: port `b28-repro`'s `group_cursor_shell_arena_exhaustion_capi_test.cpp` (`e10c8b80`): its claim cells go GREEN (a code, no exception), its controls stay GREEN.
- #647: frames per spec US1 (mid-body zero count, count last, nested mid-instance zero count, scalar `0`, absent tag), C-ABI and typed; a validator-and-read agreement cell on one frame (SC-004).
- D5: an instance past the per-instance cap: typed flag set, flat getter `WIRE_LIMIT_EXCEEDED`.
- Mutation discrimination per cell: delete each production change in turn and show its cell RED.
