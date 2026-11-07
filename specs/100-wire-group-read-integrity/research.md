# Research: 100 wire group-read integrity

Code read at `fa778a8e` (worktree `fixpp-parallel`). Locations are by symbol and quoted phrase; re-locate by symbol.

## Survey (what is there today)

| Surface | Today | Where |
|---|---|---|
| Probe cap | `kMaxBuildProbe = 128` **per insert**; on reach, `skip_insert = true`, `status_` untouched. The comment above it claims "only a crafted hostile frame can hit" it and "normal frames keep clusters far below the cap"; the D1 measurement refutes both. The file header says "robin-hood overlay"; the insert loop never displaces, so it is plain linear probing | `OffsetTable::build`, `src/wire/offset_table.cpp` (the `// DoS bound: leave this occ un-indexed` arm) |
| Other build failures | `status_ = err_…(); entries_.clear(); return;` (entry cap: `err_offset_table_full()`) | same function |
| `find()` | walk bounded by `overlay_.size()`, independent of the build's probe bound | `OffsetTable::find` |
| Parse | `Parser::parse` returns the build status's error | `include/fixpp/wire/parser.hpp`, the `build_status()` check |
| Inbound late parse failure | `dispatch_outcome::parse_failed` / `validate_outcome::parse_failed` → `close_on_late_parse_failure_()`: terminal close, no Reject, no Logout, no callback (092 C-6). 093 C-3 I-1 says every admitted frame parses; B-093-6, B-092-6, L-092-6 and the 1.11 clauses in `version.h` / `session.h` say the close is unreachable for an admitted frame | `src/session/session.cpp`; `specs/093-inbound-frame-dispositions/contracts/inbound-frame-dispositions.md` C-3 |
| Seed | one random seed per process; test hook `set_overlay_seed_for_testing` is process-global, and `CraftedCollisionSetSkipsTargetUnderMatchingSeed` sets it without restoring it | `offset_table.cpp`, `tests/support/wire_test_hooks.hpp` |
| Top-level group read | `group_slices(no_tag)` = `group_slices_status(no_tag).slices`, flag dropped | `offset_table.cpp` |
| Typed top-level | `MessageView::group<>()` builds `group_view{table_.group_slices(NoTag), ctx}` (flag defaults false); generated top-level accessors only delegate to it | `parser.hpp`; `tools/codegen/fixpp-codegen/emit_messages.cpp` |
| Typed nested | generated accessor emits `group_view<…>{r.slices, child_ctx, r.alloc_failed}` (073); the literal is pinned by `tests/codegen/accessor_resolution_census_test.cpp` | `emit_messages.cpp` |
| Group decision | dict-free → absent (#220); count tag absent → absent; count field the last entry of the table → `{first, 0}` (present); else the next field must be a member of `no_tag` in context, else absent. Count value never read. The "last entry" arm is reachable only in a sub-table over an instance slice: the root table's last entry is always CheckSum(10) | `OffsetTable::group()` |
| Validator's group decision | `group_first_field(msg_type, parent_path, tag) != 0` (context form, bare fallback on a context miss); `declared_count == 0` short-circuits | `include/fixpp/wire/validator.hpp`, `validate_group_level` / `consume_group` |
| Registration accessors | `group_first_field(msg_type, parent_path, no_tag)`: context record, else bare fallback. `group_first_field_exact`: `optional{0}` when `group_bit` is clear, the record's delimiter on a context hit, `nullopt` on a context miss. `group_bit` is set by the bare setters (`add_group_member`, `set_group_first`) and the `_ctx` setters alike | `include/fixpp/dict/table_view.hpp` |
| Bare-only registrations in production | 099's NoHops(627) on a route table whose header does not declare it: `set_group_first(kNoHops, 628)` in the bare store only; and context registration is skipped for "plain scalar reuse of the tag" | `Dictionary::as_table_view` / `build_table_view`, `src/dictionary/dictionary.cpp` |
| Delimiter hook | `dict_hooks::group_delim_fn()(opaque_dict, ctx, no_tag)` → `table_view::group_first_field(msg_type, parent_path, no_tag)`: 0 when not a group; the split in `group_slices_status` relies on its fallback ("requested and bounded") | `include/fixpp/wire/dict_hooks.hpp`; `parser.hpp` `for_table_view` |
| Group cache | `group_slices_status` pushes `group_span{.data = slices, .count = n_written, .no_tag = no_tag}` for **every** outcome of `group()` (absent, not a group, `err_group_too_large`, failed build status), inside the same `try` as the slice allocation; only a `bad_alloc` skips the push. So a limit failure is cached as an empty, not-failed row, and a failed push turns a scalar's answer into `alloc_failed` | `OffsetTable::group_slices_status` |
| Nested read | `nested_group_slices` → `resolve_nested_result`: mode (a) null sub-table, (b) sub-table slice OOM, (c) sub-build `out_of_memory`; all decided before anything asks whether `nested_no_tag` is in the instance. Sub-tables are built with the default `OffsetTable::Config` (entry cap 4096), whatever the root's caps | `offset_table.cpp`, `build_nested_subview` |
| Flat C getter | null out-pointer → `NULL_HANDLE`; writes both outputs before validating `msg`; `find` absent → `TAG_NOT_FOUND`; empty slices → `TYPE_MISMATCH` (scalar, zero count, exhaustion, group too large alike); shell `new_object` with no catch | `src/capi/message_read.cpp`, `fixpp_msg_get_group` |
| Nested C getter | returns `NULL_HANDLE` on a null `g` **before** writing its outputs; `alloc_failed` → `WIRE_LIMIT_EXCEEDED`; empty → membership-free presence probe (`scan_slice_for_tag`): absent → `TAG_NOT_FOUND`, present → `OK`/0 (a scalar too); shell `new_object` with no catch | `fixpp_group_get_nested_group` |
| C-ABI docs | CA-010 block lists `TAG_NOT_FOUND`, `TYPE_MISMATCH`, `INDEX_OUT_OF_RANGE`; `WIRE_LIMIT_EXCEEDED` undocumented for the getters. The header block says "Steady-state thunks: abort on any escaping C++ exception"; `.specify/2i-capi.md` §5.2, §5.4 and §6.2 call a steady-state exception an invariant violation (seam #5b) | `include/fix/c_api/message.h`; `.specify/2i-capi.md` |
| C-ABI version | 1.14 (099, BREAKING) | `include/fix/c_api/version.h` |
| Cache row | `group_span{data, count, no_tag}`, `sizeof <= 16` asserted | `include/fixpp/wire/offset_table.hpp` |
| Python surface | `bindings/python/fixpp.i` re-declares only `fixpp_msg_get_string` from `message.h`; `version.h` reaches Python as macros through `%include`, checked by name, not value | `bindings/python/` |

QuickFIX reference for #647 (ruling note, parent `research/reviews/ruling_B81_647_zero_count_group_v1.md`): C++ `2ce8a60` `Message::setGroup` decides with `DataDictionary::getGroup`; J `4969564` `Message.parseGroup` with `dd.getGroup` and sets the count field first. Both: count field set, group count 0, also when the declared count is not matched by instances on the wire.

## Census: tests and consumers this feature changes

Re-derive each line with its recipe at `/speckit-tasks`; the list is a condition, not a count.

**Cells that flip** (each superseded by name in tasks):
- `HostileInputHardening.CraftedCollisionSetSkipsTargetUnderMatchingSeed` (`tests/wire/hostile_input_hardening_test.cpp`): build OK + target absent → build OK + target **found** (FR-008: a single-slot collision set stays within the per-build budget). Gains seed RAII.
- `CapiInboundFrameDispositionsQ33.MsgGetGroupAtHeadroomExhaustion` (`tests/capi/inbound_frame_dispositions_capi_test.cpp`): `TYPE_MISMATCH` on exhaustion → `WIRE_LIMIT_EXCEEDED` (FR-005/FR-007).
- `CapiVersion.CApiVersionIsExactly_1_14_0` and `CapiVersion.CompositeMacroValue` (`tests/capi/version_test.cpp`) → 1.15.
- Any cell that pins the nested getter's `OK`/0 for a present scalar, or `WIRE_LIMIT_EXCEEDED` for an absent nested tag on an exhausted arena: found by the RED run of the new cells' twins over the existing suites (`ctest -L wire`, `-L capi`).

**Code that changes shape** (compile-level census):
- Readers of the result structs' `alloc_failed` field (D5 replaces it with `status`): `grep -rn '\.alloc_failed\b' src include tests tools`.
- The generated nested accessor text (D5): `emit_messages.cpp`, and its pinned literal in `tests/codegen/accessor_resolution_census_test.cpp`; any other golden of generated accessor text: `grep -rn 'r.alloc_failed' tests/codegen`. The top-level accessors are unchanged (they delegate to `MessageView::group<>()`).
- `dict_hooks_test_access::make`'s parameter list (D2's callback): `grep -rln dict_hooks_test_access tests` (`tests/fuzz/fuzz_wire_nested_slice.cpp` and wire tests among them).
- Arena bands tuned to `sizeof(OffsetTable)` (D2 grows it): `grep -rln 'sizeof(OffsetTable)' tests`; each file's header states the band's condition. Re-check, do not resize the arena: 093 C-8's `kCallbackReadHeadroom` sizing condition (`src/session/parse_capacity.hpp`, 093 data-model E-2), since a nested read places one `OffsetTable` per sub-table in the per-message arena.
- `tools/capi_freeze.sha256`: `message.h`, `version.h`, `session.h`.

**Controls that stay green**: `CraftedCollisionSetDefeatedByDifferentSeed`; `MessageReadGroup.NestedGroupEmptyGroupCountLastField`; `MessageReadFailloud.Control_CountZeroNestedGroup_AmpleArena_ReturnsOkZero`; `MessageReadFailloud.Control_AbsentNestedGroup_AmpleArena_ReturnsTagNotFound`; `MessageReadFailloud.PresentNestedGroup_ArenaExhausted_ReturnsWireLimitExceeded`; `MessageReadFailloud.RepeatedReadAfterArenaExhaustion_SignalsFailureBothTimes`; `MessageReadGroup.NonGroupTagReturnsTypeMismatch` (49 is followed by CheckSum(10), not last, so it stays `TYPE_MISMATCH`); `dict066_scalar_as_group_test`; `MessageReadGroup.DictFreeGroupReadReportsTypeMismatch` (#220); `InboundFrameDispositionsQ33.GroupSlicesAtHeadroomExhaustion` (calls the span wrapper, which is kept); `GroupSlicesKeepsWireDelimiterWhenDelimStoreAnswersZero` (B-384-2); B-004-7's witnesses; 099's NoHops route-table cells. New control: D9's D2 regression sweep over the shipped dictionaries (every top-level and nested group still a group in its own context).

**Python**: no group getter is on the Python surface, and no Python test pins the C-ABI minor; §X.7's in-repository consumer update is the SWIG regeneration every build performs. No Python behaviour cell.

## Decisions

### D1 — The probe bound is a per-build budget, and exceeding it ends the build (#645)

**Decision:** replace `OffsetTable::build`'s per-insert cap with a per-build probe budget of `kMaxBuildProbe` × `entries_.size()`, counted across the insert loop. When the budget is spent, `status_ = err_offset_table_full(); entries_.clear(); overlay_.clear(); return;`, the shape of every other build-failure arm. The constant is the same; the worst-case build stays at most `kMaxBuildProbe` probes per occurrence, so it stays linear and the DoS bound is unchanged. `find()` is untouched: its walk is already bounded by `overlay_.size()`, so a tag placed far from its home slot is still found. The refuted comment claims ("only a crafted hostile frame can hit", "normal frames keep clusters far below the cap", "robin-hood overlay") are deleted, keeping only the condition (the project's comment-claims rule: run `.claude/scripts/check-comment-claims.py` on the diff).

**Measurement (Gate A round 1, 2026-11-07; figures in the Opus review).** Recipe: replicate `mix()` and `overlay_cap_for()` as `tests/wire/hostile_input_hardening_test.cpp` does; insert n distinct tags in frame order with the build's linear-probe loop; record the longest single-insert probe and the total probes per occurrence; vary n over 20…3276 and the tag draw over 1..1000, 1..2000, 1..9999 and 1..65535, a fresh 32-bit seed per trial. Result, as conditions: consecutive tag runs and sets of up to about 200 distinct tags never reached the per-insert cap; sets of a few hundred or more distinct, non-consecutive tags at overlay load near 0.8 reached it for a small fraction of seeds; no trial's total probe work came near the per-build budget. A crafted window cluster under a known seed (D9) exceeds the budget at every size from 512 entries (a frame of about 4 KB) up, inside both the entry limit and the inbound byte limit.

**Rationale:** one ending a caller already handles: `Parser::parse` fails with `wire_offset_table_full`, the C ABI maps it to `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` (`src/capi/error.cpp`), an inbound session takes 092's late-parse-failure terminal close. With the per-insert cap that ending would also fall on non-crafted frames, and the per-process seed makes the peer's resend close the session again until the process restarts, where QuickFIX parses and delivers the frame. With the per-build budget only a frame crafted against the seed reaches it, so a terminal close is proportionate. No new `core::error` slot, so no `expected_error_map.csv` row. The ending still breaks a published promise for the crafted case, declared in D8.

**Alternatives:**
- Keep the per-insert cap with D1's ending: terminally closes sessions on legitimate traffic (the measurement). Rejected; it would be an owner decision, not a design default.
- A new `wire_index_probe_cap` error: more precise in a log, but a new slot, a CSV row and a C-ABI code mapping for an ending the caller handles identically. Rejected.
- Linear scan on a miss, or re-seeding and rebuilding: the first is the O(occ²) the cap prevents; the second is unbounded retries against an adaptive attacker. Rejected.

### D2 — Group-ness is one context-exact predicate, asked before anything else (#647)

**Decision:** `OffsetTable::group()` evaluates, in order:
1. the dict-free decline (#220), widened to decline when the bundle has no registration callback;
2. locate the count entry; absent → absent (unchanged);
3. `registered(stored context, no_tag)`; false → not a group (`err_required_field_missing`, as today's "absent" answer);
4. only then the "count field is the last entry" shortcut → present, zero instances;
5. the next field's membership: **not a member → present, zero instances** (`group_index{no_tag, first, 0}`), where today it returns absent;
6. otherwise today's extent walk, unchanged (`consume_group_extent` and the split are untouched).

`registered` is true on a **context record** for `(msg_type, parent_path, no_tag)` (members or delimiter; a record whose delimiter is 0, as a hand-built `add_group_member_ctx` leaves it, still counts), or when `no_tag` has **no** context record in any context and `group_bit(no_tag)` is set (a bare-only registration). The count's value is never read; there is no `declared == 0` branch and no strict-zero parser.

It is backed by:
- a per-tag "has a context record" bitset in `table_view`, beside `group_bits_`, set only by `set_group_first_ctx` / `add_group_member_ctx`, built once with the table (never on the read path);
- a `noexcept` accessor beside `group_first_field_exact` that answers the predicate;
- one `noexcept` `dict_hooks` callback (`group_registered_fn`) filled by `for_table_view`. `group_delim_fn` keeps its fallback: the split relies on it.

**Rationale:**
- It is the ruling's constraint stated exactly: registration "for `(msg_type, parent_path, no_tag)`", never the value.
- The second arm keeps every bare-only registration a group: hand-built fixtures, B-384-2's members-only fixture, 099's NoHops on route tables (which have context records for their body groups, so an "exact only when the table has no context records" rule would make NoHops not a group).
- A nested group's count tag read at the top level (802 at the root) misses its context, has context records elsewhere, and is not a group: the cross-context phantom the ruling forbids is closed. Today's flat getter for 802 with a value `n` reaches a cursor through the bare member fallback (derived from source, not run; the RED run on `fa778a8e` confirms or corrects it).
- Asking it before the "last entry" shortcut makes a scalar that is last in its instance not a group, so one condition (a present scalar) has one ending.

**Size census:** the callback grows `dict_hooks`, so `OffsetTable` and every other by-value holder (`MessageView`, `entry_context`, `field_iterator`). See the census above (arena bands tuned to `sizeof(OffsetTable)`, 093's `kCallbackReadHeadroom` re-check, the test seam's parameter list). An equivalent that does not grow `OffsetTable` (for example answering the predicate through an existing callback's signature) is acceptable if found at tasks; the predicate and its witnesses do not change with it.

**Validator:** unchanged (FR-009). SC-004 holds for every frame whose count tag is registered in its exact context, the only case where a zero-count group exists by the dictionary's own account. The validator's remaining context-miss fallback (a scalar-reuse tag holding `n > 0` makes `consume_group` look for a delimiter) is a lead to file.

**Alternatives:**
- The delimiter oracle with its bare fallback (this bundle's first draft): context-blind, so 802 at the root becomes a phantom group. Rejected (the ruling).
- `group_first_field_exact` alone, or exact registration with a fallback only "when the table contains no contextual group records at all": makes 099's NoHops not a group on route tables. Rejected.
- Decide by the value 0: a scalar `0` becomes a phantom group, and `parse_bounded_u32` reads `""`, `abc` and `-1` as 0. Rejected.

### D3 — The group read result says whether the tag is a group (#647, FR-003)

**Decision:** `group_slices_result` and `nested_slices_result` gain `bool is_group` (true when D2 answered "registered", including zero instances read). The cache row `group_span` gains the same flag; `8 + 4 + 2 + 1` bytes stays within the asserted `sizeof(group_span) <= 16`. The span wrapper `group_slices()` stays as is.

**Rationale:** the C getters must tell "zero instances" (`OK`/0) from "scalar" (`TYPE_MISMATCH`), and both read an empty span today. A by-value `bool` on trivially copyable structs: no allocation, no new call.

**Alternatives:** call `group()` again from the getter (a second O(n) scan per read, and two places to keep in agreement). Rejected.

### D4 — C-ABI getters: one ordered table of endings (#541, #644, #647)

**Decision:** both getters answer from the one ordered table in `contracts/capi-group-getters.md` (first condition that holds): argument errors; absent → `TAG_NOT_FOUND`; read could not complete → `WIRE_LIMIT_EXCEEDED`; not registered → `TYPE_MISMATCH`; registered, zero instances read → `OK`/0, `*out` null; n instances → `OK`, cursor. The table is true for both getters because the wire layer guarantees **failed ⇒ present** (D11): the nested getter may test the status before its presence probe and still match the table.

- Flat getter: `find` (allocation-free) → absent; `group_slices_status`: status ≠ ok → `WIRE_LIMIT_EXCEEDED`; `!is_group` → `TYPE_MISMATCH`; empty → `OK`/0; else the cursor.
- Nested getter: checks its two out-pointers first (`NULL_HANDLE`, outputs untouched, when either is null), then writes null/0 to both before validating `g` and the index (row 0b, as the flat getter does); `nested_group_slices`: status ≠ ok → `WIRE_LIMIT_EXCEEDED`; `!is_group` → its existing presence probe: absent → `TAG_NOT_FOUND`, present → `TYPE_MISMATCH`; empty → `OK`/0; else the cursor.
- The cursor-shell `new_object` sits in `try { … } catch (std::bad_alloc const&) { return FIXPP_ERR_WIRE_LIMIT_EXCEEDED; }` with the out-parameters left null/0.

**Rationale:** `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` ("Wire-level capacity or size limit exceeded (DoS bound)") is what the nested getter returns today and what `wire_offset_table_full` / `wire_group_too_large` already map to. One code for one cause (FR-007).

**Alternatives:** a `FIXPP_ERR_OUT_OF_MEMORY`-style code: the arena is a fixed per-message bound, not process memory; and the nested getter's shipped answer would change too. Rejected. Separate ordered tables per getter with "exhausted + absent nested = `WIRE_LIMIT_EXCEEDED`": writes into the contract an ending 073 FR-007 forbids, and one that depends on residual arena bytes. Rejected (D11).

### D5 — A group read stopped by a wire limit is a failed read, with its own status (found at plan)

**Finding:** `OffsetTable::group()` returns `err_group_too_large` when an instance exceeds `max_group_entries_per_instance` (default 4096) or nesting exceeds 16. `group_slices_status` turns that into an empty, not-failed result, so the typed read is empty and the flat getter answers `TYPE_MISMATCH`. A peer reaches the instance cap with a frame that repeats a member tag past 4096 in one instance (a 65536-byte frame holds that), on a validation-off session. Same class as #644 (a present group read as empty); not filed.

**Decision:** the two result structs carry one `std::uint8_t`-based enum `group_read_status { ok, alloc_failed, limit_exceeded }` (field `status`) in place of `bool alloc_failed`. `alloc_failed` is set for a caught `bad_alloc` (slices, sub-table shell, sub-build `out_of_memory`); `limit_exceeded` for `wire_group_too_large` and for a sub-build `wire_offset_table_full` (D7). `group_view` keeps `alloc_failed()` (`status == alloc_failed`, its documented meaning unchanged) and gains `read_failed()` (`status != ok`); its doc tells typed readers to check `read_failed()`. Its constructors' trailing parameter becomes the status (defaulted `ok`). The C ABI maps any `status != ok` to `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`.

**Codegen consequence:** the generated nested accessor passes `r.status` where it passes `r.alloc_failed` today: one token in `emit_messages.cpp`'s nested accessor line, regenerated text for every nested accessor, and the pinned literal in `accessor_resolution_census_test.cpp` (census). Passing the enum to the old `bool` parameter would make `limit_exceeded` read as `alloc_failed() == true`, the lie this decision avoids, so the emitter change is required for the typed nested witness.

**Alternatives:** widen `alloc_failed()`'s meaning (the first draft): makes a public accessor lie about hostile structural input. Rejected. Two flags (`alloc_failed`, `limit_exceeded`) that every reader must OR. Rejected. Leave D5 out and file it: keeps a silent loss on lines this feature rewrites. Rejected.

### D6 — Typed top-level read reports the status (#644)

**Decision:** `MessageView::group<>()` calls `group_slices_status(NoTag)` and constructs `group_view{r.slices, ctx, r.status}`. The generated top-level accessors delegate to `group<>()`, so their text does not change (the nested accessors' does, D5).

### D7 — A nested sub-table whose build failed for a wire limit is a failed read (found at plan)

**Decision:** `resolve_nested_result`'s mode (c) widens from "sub-build `out_of_memory`" (`alloc_failed`) to also "sub-build `wire_offset_table_full`" (`limit_exceeded`), so a sub-table over its entry cap or D1's budget is reported, not read as an empty nested group. Other sub-build errors keep 073's not-failed scope: the read answers as for a tag it cannot establish as a group.

**Reachability without a seed:** `build_nested_subview` builds the sub-table with the default `OffsetTable::Config` (entry cap 4096) whatever the root's caps. A `Parser::parse` with raised `max_offset_entries` and `max_group_entries_per_instance` over an instance of more than 4096 entries that carries a nested group makes the sub-table fail with `wire_offset_table_full`. Today that is not OOM, so mode (c) does not fire, and the nested getter reads `OK`/0 and the typed view empty and not failed: a pre-existing silent loss on the public cap API, independent of D1.

**Alternative:** have the sub-table inherit the root's `Config` (removes that path, but changes the caps nested reads run under, and D1's budget can still fail a sub-build). Recorded as a lead (spec Out of Scope).

### D8 — C-ABI version (FR-010)

**Decision:** C-ABI **1.15, BREAKING** (`[const §X.7]`). No symbol, struct or code is added. Affected, each marked BREAKING in its documentation:
- `fixpp_msg_get_group` and `fixpp_group_get_nested_group` (`include/fix/c_api/message.h`): the CA-010 block and both functions' docs carry the contract's table and its "Changes from 1.14"; the header block's "Steady-state thunks: abort on any escaping C++ exception" is amended for the two getters, whose cursor-shell `bad_alloc` is now a documented return.
- The session observers whose 1.11 clause says "a frame the session admits always parses for dispatch" (`include/fix/c_api/session.h`): amended in place, marked 1.15, for a frame crafted against the index seed (D1).
- The 1.11 entry and a new 1.15 entry in `include/fix/c_api/version.h`'s history, naming this feature.
- `tools/capi_freeze.sha256` refreshed for `message.h`, `version.h`, `session.h`; `tests/capi/version_test.cpp` to 1.15.

### D9 — Witnesses (SC-001)

Every cell names the requirement it witnesses; each production change has a mutation that turns its cell RED.

- **#645 (D1).** A window-cluster generator under `set_overlay_seed_for_testing`: with the known seed and the mask of the real entry count (envelope tags included), take the k distinct tags whose home slots are lowest (one window of slots) and insert them highest-home first, drawing only from tags that are not envelope or session-header tags and not standard Length/Data pair tags (so the frame cannot fail on framing or splitting instead); parse fails with `wire_offset_table_full`. Mutation: revert the budget; the frame parses (and, in the session cell, the callback fires), proving the cell measures the budget. A non-crafted frame from the D1 measurement that reaches today's per-insert cap: parses, every tag found (RED today). The existing single-slot cell flips to "found, build OK"; its different-seed sibling stays a control. **Inbound session cell (required):** the crafted frame, under the seed hook set before the session's first parse, closes the session terminally with no Reject and no callback. Every seed-setting cell restores the prior seed by RAII.
- **#644 (D6).** A top-level group whose slice allocation fails (the Q33 null-upstream headroom rig), read through `MessageView::group<>()`: `alloc_failed()` and `read_failed()` true. `InboundFrameDispositionsQ33.GroupSlicesAtHeadroomExhaustion` calls the span wrapper and stays a control; it is not this witness. Flat getter `WIRE_LIMIT_EXCEEDED`; the Q33 flat cell flips.
- **#541 (D4).** Port `b28-repro`'s `group_cursor_shell_arena_exhaustion_capi_test.cpp` (`e10c8b80`): each claim cell asserts `last_rc == FIXPP_ERR_WIRE_LIMIT_EXCEEDED` (the port's `LoopEnd::by_error` alone accepts any non-OK); its controls stay GREEN.
- **#647 (D2, D3).** Flat: registered zero count mid-body and as the last body field; lying count; 802 at the root with value 0 and with `n`; tag 10 (control). Nested: zero count mid-instance; present scalar mid-instance **and** last in instance; absent tag. Typed: the same frames. Bare-only registration: NoHops on a route table, and the B-384-2 fixture (controls). SC-004: a validator-and-read agreement cell on one frame registered in its exact context. Mutation: replace the predicate with today's next-field membership; the scalar-last and lying-count cells go RED.
- **D5.** Top level: an instance past the per-instance cap, read twice: typed `read_failed()` true and `alloc_failed()` false; flat getter `WIRE_LIMIT_EXCEEDED` both times. Nested: the same inside a sub-table, read twice, C-ABI and typed.
- **D7.** The raised-root-cap frame above: nested getter `WIRE_LIMIT_EXCEEDED` and typed `read_failed()`, first and repeat read. Mutation: delete D7's widening.
- **D10.** A present scalar read on an exhausted arena: `TYPE_MISMATCH` on both getters (nested RED today: `alloc_failed` from the sub-table or the cache push; flat a control, since it drops the flag). At the wire layer, the same read through `group_slices_status` reports `status == ok`, `!is_group` (RED today: `alloc_failed`). Repeat reads for every failure origin (slices, shell, limit) return the first read's code.
- **D11.** An absent nested tag on an exhausted arena: C-ABI `TAG_NOT_FOUND`, typed `!read_failed()`. Mutation: delete the presence scan; both cells go RED.
- **Row 0.** The nested getter with a null `g` and both outputs non-null: `NULL_HANDLE`, outputs null/0 (RED today); either getter with one out-pointer null: `NULL_HANDLE`, the other output untouched (control).
- **D2 regression sweep (FR-009).** Over every shipped dictionary and the route tables: every group a message places at its top level (body, header, trailer) satisfies the registration predicate in `(msg_type, [])`, and every nested group satisfies it under its parent path. A header-group control on a non-route table (for example a header group of a FIX44 message read through the flat getter) stays a cursor. Prove the sweep can fail before trusting it: delete the predicate's bare arm, and separately key it on a wrong parent path; each mutant turns the sweep RED.
- Lanes: the arena-exhaustion cells assert only under a null arena upstream; on MSVC debug they skip. The verify record names, per cell, the lanes that show RED and those that skip.

### D10 — The group cache records only completed reads (FR-007)

**Decision:** in `group_slices_status`, cache a row only for a completed read: a registered group with its slices materialized (including zero instances), or a not-a-group answer. A failed read (`alloc_failed` or `limit_exceeded`) caches nothing, so a repeat read recomputes and fails with the same status (`group()` allocates nothing, so a limit failure recurs deterministically). The cache `push_back` gets its own `try`: if it throws, the computed result is served uncached, as `nested_group_slices` already does for its own `nested_cache_` insert ("degrade to rebuild next time"). So an answer that needed no memory (not a group) never becomes a failure because the cache did not fit, and a materialized group whose row did not fit is still returned.

**Alternative:** cache the status in `group_span` too and replay it. Workable within 16 bytes, but caching an outcome is what made the first draft's repeat read differ from the first; recomputing has one source of truth. Rejected.

### D11 — On the nested path, a failed read means a present tag (FR-004, 073 FR-007)

**Decision:** `resolve_nested_result` takes the instance slice and the `hooks` argument of the `nested_group_slices` call (the caller's bundle), at both of its exits. When any failure mode fires (a, b, c, D7), it scans the slice for `nested_no_tag` without allocating: a field walk that splits Length+Data through `hooks.data_tag_for_length`, the rule `OffsetTable::build` uses. Absent → `{empty, ok, not a group}`; present → the failed status. It never uses `this->hooks_`: pairing the root table's own bundle with the caller's split would reintroduce the mismatched-pairing defect fixpp#426 closed. (`scan_slice_for_tag` is local to `src/capi/message_read.cpp` and uses `parser.hpp`'s `field_iterator`, which `offset_table.hpp` cannot include, so the wire layer has its own walk.)

**Rationale:** costs nothing on the success path; meets 073 FR-007 and FR-004 without qualification; the typed table holds as written; and the C getters' one ordered table follows.

**Alternative:** decide presence in the C getter before calling the wire layer: fixes the C ABI only, and the typed nested accessor would still report `alloc_failed()` for an absent group. Rejected.
