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
| Registration accessors | `group_first_field(msg_type, parent_path, no_tag)`: context record, else bare fallback. `group_first_field_exact`: `optional{0}` when `group_bit` is clear, the record's delimiter on a context hit, `nullopt` on a context miss. `group_bit` is set by the bare setters (`add_group_member`, `set_group_first`) and the `_ctx` setters alike; each of the three `_ctx` setters (`add_group_member_ctx`, `add_group_required_member_ctx`, `set_group_first_ctx`) creates a `group_ctx_` key. `is_header_tag(tag)` (098) reads `header_bits_`, filled from the dictionary's declared header; `collect_header_tags` (`src/dictionary/xml_loader.cpp`) records a header `<group>`'s count tag and descends into it; it is empty for the FIX 5.0 XMLs | `include/fixpp/dict/table_view.hpp` |
| Bare-only registrations in production | 099's NoHops(627) on a route table whose header does not declare it: `set_group_first(kNoHops, 628)` in the bare store only; and context registration is skipped for "plain scalar reuse of the tag" | `Dictionary::as_table_view` / `build_table_view`, `src/dictionary/dictionary.cpp` |
| Membership hook | `for_table_view`'s membership lambda calls `table_view::group_member_tags(msg_type, parent_path, no_tag)`, which falls back to the bare store on a context miss, and `as_table_view` fills the bare store for every group (the dual store). So a body group read in a context the dictionary does not declare (a message that does not declare it, or an undeclared MsgType) reads as a group today (derived from source; the RED run confirms or corrects it). The lambda's comment says that fallback is "unreachable HERE"; this case refutes it. Header groups are recorded per declared message (`expand_field_list(header_node_, …)` inside each message's expansion, `src/dictionary/xml_loader.cpp`) | `include/fixpp/wire/parser.hpp` `for_table_view`; `src/dictionary/dictionary.cpp` |
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
- Any cell that pins the nested getter's `OK`/0 for a present scalar, `WIRE_LIMIT_EXCEEDED` for an absent nested tag or a present scalar on an exhausted arena, or a cursor for a body group read in a context its dictionary does not declare: found by the RED run of the new cells' twins over the existing suites (`ctest -L wire`, `-L capi`).

- Any cell or arena band that pins a read served after its cache row failed to fit, or the order in which allocations fail under exhaustion: D10 moves the flat push before the slices (n ≥ 1) and the nested push before the sub-table build, so such a read now answers `alloc_failed`. Found by the RED run; look first in `tests/wire/nested_group_slices_cache_test.cpp` and the failloud bands named under "Arena bands" below.

**Code that changes shape** (compile-level census):
Every recipe here runs repo-wide, with no `--include` filter: oracles live under `specs/` too.

- Readers of the result structs' `alloc_failed` field (D5 renames it to `status`): `command grep -rnE '\.alloc_failed\b' include src tests tools specs`. Hits fall in three kinds. Code and tests are edited. The checked-in generated goldens under `specs/003-dictionary-codegen/contracts/golden/` and `specs/076-fix-latest-typed-codegen/contracts/golden/` are live oracles (`tests/codegen/determinism_test.cpp` through `FIXPP_CODEGEN_GOLDEN_DIR` and `FIXPP_CODEGEN_076_GOLDEN_DIR`, and `tests/codegen/read_tier_byte_diff_test.cmake`): they are regenerated by the emitter, never hand-edited, and are SC-003's named exception. Shipped feature bundles (`specs/073-nested-read-arena-failloud/`, and `specs/004-wire-codec/contracts/group_view.hpp` for the constructors) are history: flagged in `brain/components/wire.md` (Phase 6), never edited.
- The generated nested accessor text (D5): `emit_messages.cpp`, its pinned literal in `tests/codegen/accessor_resolution_census_test.cpp`, and the goldens above. The top-level accessors are unchanged (they delegate to `MessageView::group<>()`).
- By-value holders of `dict_hooks` (D2 grows it): `command grep -rnE 'dict_hooks +[A-Za-z_]+ *(\{ *\})?;' include src`. The recipe must report `Parser`'s member, spelt with a space before its braces; a recipe without ` *` before the brace misses it, so that member is the known positive to check first. Re-derive the list; at the head this bundle was written against it names `OffsetTable`, `MessageView`, `Parser`, `entry_context`, `field_iterator`, `body_builder` and `length_data_checker`. Also re-derive the sufficiency condition on `nested_cache_row::hooks_key` (`offset_table.hpp`) with the registration callback added; a seam caller must not pair one `opaque_dict` with two registration stubs.
- Size pins on the stores D2's section arm adds to `table_view` and `dict_metadata_handle`: `command grep -rnE 'sizeof\((fixpp::dict::)?(table_view|dict_metadata_handle)\)' include src tests`. At `cbcc1473` it reports no pin. Re-run it at `/speckit-tasks`, and prove it with a seeded match first.
- `dict_hooks_test_access::make`'s parameter list (D2's callback): `command grep -rln dict_hooks_test_access include src tests tools` (`tests/fuzz/fuzz_wire_nested_slice.cpp` and wire tests among them). Each caller passes a registration stub that agrees with its membership stub. The fixtures that pass a **null** delimiter callback with a non-null membership callback (`tests/wire/nested_group_slices_cache_test.cpp`, the `divergent_member_fn` probe in `tests/wire/typed_read_split_agreement_test.cpp`, the null-delimiter arm of `tests/wire/offset_table_test.cpp`) get a stub that keeps their pinned outcome.
- Arena bands tuned to `sizeof(OffsetTable)` (D2 grows it): `command grep -rln 'sizeof(OffsetTable)' include src tests`; each file's header states the band's condition and that it does not port across toolchains, and each band is re-measured per that header. Do not resize the arena. 093 C-8's `kCallbackReadHeadroom` sizing condition (`src/session/parse_capacity.hpp`, 093 data-model E-2) compares the room left after a parse with the base arena's whole size, so it is re-read, not re-sized.
- `.specify/2b-wire.md` sentences D1 falsifies: `command grep -nE 'wire_offset_table_full|robin-hood' .specify/2b-wire.md`. Each hit that states the error's cause or the overlay's probing gets an "Updated (100-wire-group-read-integrity): …" note in the doc's existing convention (098 and 099 used it), in the same change as D1's comment deletion (Phase 2).
- `tools/capi_freeze.sha256`: `message.h`, `version.h`, `session.h`.

**Controls that stay green**: `CraftedCollisionSetDefeatedByDifferentSeed`; `MessageReadGroup.NestedGroupEmptyGroupCountLastField`; `MessageReadFailloud.Control_CountZeroNestedGroup_AmpleArena_ReturnsOkZero`; `MessageReadFailloud.Control_AbsentNestedGroup_AmpleArena_ReturnsTagNotFound`; `MessageReadFailloud.PresentNestedGroup_ArenaExhausted_ReturnsWireLimitExceeded`; `MessageReadFailloud.RepeatedReadAfterArenaExhaustion_SignalsFailureBothTimes`; `MessageReadGroup.NonGroupTagReturnsTypeMismatch` (49 is followed by CheckSum(10), not last, so it stays `TYPE_MISMATCH`); `dict066_scalar_as_group_test`; `MessageReadGroup.DictFreeGroupReadReportsTypeMismatch` (#220); `InboundFrameDispositionsQ33.GroupSlicesAtHeadroomExhaustion` (calls the span wrapper, which is kept); `GroupSlicesKeepsWireDelimiterWhenDelimStoreAnswersZero` (B-384-2); B-004-7's witnesses; 099's NoHops route-table cells. New controls: US1.10's header group in an undeclared MsgType, and D9's D2 regression sweep over the shipped dictionaries.

**Python**: no group getter is on the Python surface, and no Python test pins the C-ABI minor; §X.7's in-repository consumer update is the SWIG regeneration every build performs. No Python behaviour cell.

## Decisions

### D1 — The probe bound is a per-build budget, and exceeding it ends the build (#645)

**Decision:** replace `OffsetTable::build`'s per-insert cap with a per-build probe budget of `kMaxBuildProbe` × `entries_.size()`, counted across the insert loop. When the budget is spent, `status_ = err_offset_table_full(); entries_.clear(); overlay_.clear(); return;`, the shape of every other build-failure arm. The constant is the same; the worst-case build stays at most `kMaxBuildProbe` probes per occurrence, so it stays linear and the DoS bound is unchanged. `find()` is untouched: its walk is already bounded by `overlay_.size()`, so a tag placed far from its home slot is still found. The refuted comment claims ("only a crafted hostile frame can hit", "normal frames keep clusters far below the cap", "robin-hood overlay") are deleted, keeping only the condition (the project's comment-claims rule: run `.claude/scripts/check-comment-claims.py` on the diff). The same claims in `.specify/2b-wire.md` get "Updated (100-wire-group-read-integrity)" notes in the same change (census), so no copy is left behind.

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

**The predicate.** This is the one statement of its arms; the spec, the data model and the contract point here.
`registered(ctx, no_tag)` holds when any of these arms holds:
1. **Context arm:** `group_ctx_` has a key for `(msg_type, parent_path, no_tag)`, whatever its record holds: members, a delimiter, or required members only. A record whose delimiter is 0, as a hand-built `add_group_member_ctx` leaves it, still counts.
2. **Section arm:** `table_view` holds a section-group record for `(parent_path, no_tag)`: the dictionary declares a group with count tag `no_tag` under `parent_path` inside its header or its trailer, whatever the message type (QuickFIX C++ `_header_` / `_trailer_`, QuickFIX/J `HEADER_ID`). `is_header_tag` and `group_bit` are not consulted. *(Supersedes round 2's header arm, `parent_path` empty ∧ `is_header_tag` ∧ `group_bit`, which is not a section registration: superseded by Gate A round 3 hand-edit (owner, 2026-11-07).)*
3. **Bare-only arm:** `no_tag` has no context key in any context, and `group_bit(no_tag)` is set (a bare-only registration).

Otherwise the tag is not registered. The count's value is never read; there is no `declared == 0` branch and no strict-zero parser.

The arms follow QuickFIX's lookup keys. C++ `2ce8a60` `DataDictionary::getGroup(msg, field, …)` (`DataDictionary.h`) requires a `(field, msg)` entry for a body group. Header fields are parsed with `setGroup("_header_", …)` (C++ `Message::setString`) and `DataDictionary.HEADER_ID` (J), keys that do not depend on MsgType. fixpp's loader records header or trailer groups, at any depth, per declared message (survey, membership hook), so without arm 2 a header or trailer group, at any depth, in an undeclared MsgType would stop reading as a group.

The records are filled by the walks that fill `header_tags_` (`collect_header_tags` in both loaders), extended to the trailer (XML `<trailer>`, Orchestra component 1025) and to carry each group's parent path. A group is recorded with the path of section groups enclosing it, never through a body context. *(Round 2's five "Conditions arm 2 rests on" are deleted: the record is structural, so none is needed. Superseded by Gate A round 3 hand-edit (owner, 2026-11-07).)*

**Who asks it:** `group()` (step 3 above), and `resolve_nested_result`'s failure branch (D11), both through the read's context and the caller's hooks. It is O(1) and allocates nothing.

It is backed by:
- a per-tag "has a context key" bitset in `table_view`, beside `group_bits_`. It is set wherever a `group_ctx_` key is created: in all three `_ctx` setters (`add_group_member_ctx`, `add_group_required_member_ctx`, `set_group_first_ctx`). It is built once with the table, never on the read path;
- a MsgType-free set of `(parent_path, no_tag)` section-group keys in `table_view`, filled by a new `table_view_builder::add_section_group(parent_path, no_tag)` that `as_table_view` calls from a new handle store (`dict_metadata_handle`, beside `header_tags_`). Built once with the table; no arena impact, since `table_view` is per dictionary. The "has a context key" bitset is unchanged; section records do not set it;
- a `noexcept` accessor beside `group_first_field_exact` that answers the predicate;
- one `noexcept` `dict_hooks` callback (`group_registered_fn`), filled by `for_table_view`. `group_delim_fn` keeps its fallback, because the split relies on it. The registration answer is a separate callback, not folded into `group_delim_fn` (plan.md `### Round 2 — disagreements`).

**Rationale:**
- It is the ruling's constraint stated exactly: registration "for `(msg_type, parent_path, no_tag)`", never the value.
- Arm 3 keeps every bare-only registration a group: hand-built fixtures, B-384-2's members-only fixture, and 099's NoHops on route tables. Route tables have context records for their body groups, so an "exact only when the table has no context records" rule would make NoHops not a group.
- A nested group's count tag read at the top level (802 at the root) misses its context, has context records elsewhere, and is not a group: the cross-context phantom the ruling forbids is closed. Today's flat getter for 802 with a value `n` reaches a cursor through the bare member fallback (derived from source, not run; the RED run on `fa778a8e` confirms or corrects it).
- A body group read in a context the dictionary does not declare is not registered, as QuickFIX `getGroup` answers. Today the membership hook's bare fallback reads it as a group (survey), so this is a declared change: contract "Changes from 1.14", and a B&L row (Phase 6). The membership lambda's "unreachable HERE" comment is deleted in Phase 2 and not restated (the comment-claims rule).
- Asking it before the "last entry" shortcut makes a scalar that is last in its instance not a group, so one condition (a present scalar) has one ending.

**Size census:** the callback grows `dict_hooks`, and so every by-value holder of it. The section store grows `table_view` and `dict_metadata_handle`. The holders, the test seam's stubs, the arena re-checks and the size-pin recipe for the two stores are listed in the census above.

**Validator:** unchanged (FR-009). SC-004 holds for every frame whose count tag is registered in its exact context, the only case where a zero-count group exists by the dictionary's own account. The validator's remaining context-miss fallback (a scalar-reuse tag holding `n > 0` makes `consume_group` look for a delimiter) is a lead to file.

**Alternatives:**
- The delimiter oracle with its bare fallback (this bundle's first draft): context-blind, so 802 at the root becomes a phantom group. Rejected (the ruling).
- `group_first_field_exact` alone, or exact registration with a fallback only "when the table contains no contextual group records at all": makes 099's NoHops not a group on route tables. Rejected.
- Decide by the value 0: a scalar `0` becomes a phantom group, and `parse_bounded_u32` reads `""`, `abc` and `-1` as 0. Rejected.

### D3 — The group read result says whether the tag is a group (#647, FR-003)

**Decision:** `group_slices_result` and `nested_slices_result` gain `bool is_group` (defined in data-model `group_slices_result`). The cache row `group_span` gains the same flag; `8 + 4 + 2 + 1` bytes stays within the asserted `sizeof(group_span) <= 16`. The span wrapper `group_slices()` stays as is.

**Rationale:** the C getters must tell "zero instances" (`OK`/0) from "scalar" (`TYPE_MISMATCH`), and both read an empty span today. A by-value `bool` on trivially copyable structs: no allocation, no new call.

A repeat read is answered by the `group_index_` loop at the top of `group_slices_status`, a separate path from the one that computes the row. A zero-count group and a not-a-group answer both cache `count == 0`, so on a hit only the cached `is_group` separates them. D9's repeat-read twins witness it.

**Alternatives:** call `group()` again from the getter (a second O(n) scan per read, and two places to keep in agreement). Rejected.

### D4 — C-ABI getters: one ordered table of endings (#541, #644, #647)

**Decision:** both getters answer from the one ordered table in `contracts/capi-group-getters.md` (first condition that holds): argument errors; absent → `TAG_NOT_FOUND`; read could not complete → `WIRE_LIMIT_EXCEEDED`; not registered → `TYPE_MISMATCH`; registered, zero instances read → `OK`/0, `*out` null; n instances → `OK`, cursor. The table is true for both getters because a failed read means a present, registered tag (the invariant is stated in data-model `nested_slices_result`; D11 establishes it). So the nested getter may test the status before its presence probe and still match the table.

- Flat getter: `find` (allocation-free) → absent; `group_slices_status`: status ≠ ok → `WIRE_LIMIT_EXCEEDED`; `!is_group` → `TYPE_MISMATCH`; empty → `OK`/0; else the cursor.
- Nested getter: checks its two out-pointers first (`NULL_HANDLE`, outputs untouched, when either is null), then writes null/0 to both before validating `g` and the index (row 0b, as the flat getter does); `nested_group_slices`: status ≠ ok → `WIRE_LIMIT_EXCEEDED`; `!is_group` → its existing presence probe: absent → `TAG_NOT_FOUND`, present → `TYPE_MISMATCH`; empty → `OK`/0; else the cursor.
- The cursor-shell `new_object` sits in `try { … } catch (std::bad_alloc const&) { return FIXPP_ERR_WIRE_LIMIT_EXCEEDED; }` with the out-parameters left null/0.

**Rationale:** `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` ("Wire-level capacity or size limit exceeded (DoS bound)") is what the nested getter returns today and what `wire_offset_table_full` / `wire_group_too_large` already map to. One code for one cause (FR-007).

**Alternatives:** a `FIXPP_ERR_OUT_OF_MEMORY`-style code: the arena is a fixed per-message bound, not process memory; and the nested getter's shipped answer would change too. Rejected. Separate ordered tables per getter with "exhausted + absent nested = `WIRE_LIMIT_EXCEEDED`": writes into the contract an ending 073 FR-007 forbids, and one that depends on residual arena bytes. Rejected (D11).

### D5 — A group read stopped by a wire limit is a failed read, with its own status (found at plan)

**Finding:** `OffsetTable::group()` returns `err_group_too_large` when an instance exceeds `max_group_entries_per_instance` or nesting exceeds its depth limit. `group_slices_status` turns that into an empty, not-failed result, so the typed read is empty and the flat getter answers `TYPE_MISMATCH`. Same class as #644 (a present group read as empty); not filed.

**Reachability, as conditions.**
- **Top level: reachable.** A session's root table runs with `Config{.max_offset_entries = inbound_entry_cap_}` and the default per-instance cap (`src/session/session.cpp`). Where the root's entry cap exceeds the per-instance cap, a peer reaches the instance cap with a frame that repeats a member tag past it in one instance, on a validation-off session.
- **Nested: defensive.** `build_nested_subview` builds the sub-table with the default `Config`, whose entry cap (`default_max_offset_entries`) equals the per-instance cap (`default_max_group_entries_per_instance`). `consume_group_extent` flags an instance only when it holds more entries than the per-instance cap, so a sub-table that large fails `OffsetTable::build`'s entry-cap check first (D7). The nested walk starts at its stored context's depth and descends the same members with the same bundle as the root walk that produced the parent instance, so a parent read that completed bounds its depth. The one exception is a caller passing a different bundle to the six-argument `nested_group_slices`; the C getter (the four-argument overload, which uses `this->hooks_`) and the generated accessors (`ctx_.hooks`) do not. The status mapping is kept, since it costs nothing, and the RED phase records the arm as unreachable, as contract row 3's malformed-data arm.

**Decision:** the two result structs carry one `std::uint8_t`-based enum `group_read_status { ok, alloc_failed, limit_exceeded }` (field `status`) in place of `bool alloc_failed`. `alloc_failed` is set for a caught `bad_alloc` (slices, the cache row of a group with instances (D10), the sub-table shell or its cache row, a sub-build `out_of_memory`). `limit_exceeded` is set for `wire_group_too_large` and for a sub-build `wire_offset_table_full` (D7). `group_view` keeps `alloc_failed()` (`status == alloc_failed`, its documented meaning unchanged) and gains `read_failed()` (`status != ok`); its doc tells typed readers to check `read_failed()`. Constructors:
- both `bool` constructors are kept as they are (`true` → `alloc_failed`);
- two status constructors are added **without** a default argument, so `group_view{inst, ctx}` still selects the `bool` overload unambiguously;
- `MessageView::group<>()` (D6) and the emitter pass `r.status`.

The C ABI maps any `status != ok` to `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`. Renaming the result structs' field is a C++ source change for any reader of it, including a header generated by an older emitter; it is declared in the B&L delta (Phase 6).

**Codegen consequence:** the generated nested accessor passes `r.status` where it passes `r.alloc_failed` today, because the field is renamed. That is one token in `emit_messages.cpp`'s nested accessor line, regenerated text for every nested accessor (the checked-in goldens included, census), and the pinned literal in `accessor_resolution_census_test.cpp`. An `enum class` does not convert to `bool`, so an emitter left on the old field or passing the status to a `bool` parameter is a compile error, not a silent misreport; it needs no mutant of its own.

**Alternatives:** widen `alloc_failed()`'s meaning (the first draft): makes a public accessor lie about hostile structural input. Rejected. Two flags (`alloc_failed`, `limit_exceeded`) that every reader must OR. Rejected. Replace the constructors' `bool` parameter with the status: `group_view{…, true}` in caller source stops compiling, for no gain. Rejected. Leave D5 out and file it: keeps a silent loss on lines this feature rewrites. Rejected.

### D6 — Typed top-level read reports the status (#644)

**Decision:** `MessageView::group<>()` calls `group_slices_status(NoTag)` and constructs `group_view{r.slices, ctx, r.status}`. The generated top-level accessors delegate to `group<>()`, so their text does not change (the nested accessors' does, D5).

### D7 — A nested sub-table whose build failed for a wire limit is a failed read (found at plan)

**Decision:** `resolve_nested_result`'s mode (c) widens from "sub-build `out_of_memory`" (`alloc_failed`) to also "sub-build `wire_offset_table_full`" (`limit_exceeded`), so a sub-table over its entry cap or D1's budget is reported, not read as an empty nested group. Other sub-build errors keep 073's not-failed scope: the read answers as for a tag it cannot establish as a group.

**Reachability, as conditions.**
- A sub-table is built with the default `Config` whatever the root's caps (`build_nested_subview`). A session parses with `Config{.max_offset_entries = inbound_entry_cap_}` and the default per-instance cap (`src/session/session.cpp`).
- Where the dictionary's delimiter (`group_delim_fn`, which `group_slices_status` splits on) matches the wire's first tag (which `consume_group_extent` measures), each slice is one walk instance. The per-instance cap bounds it, so it fits the sub-table's entry cap.
- So a sub-table over its entry cap needs a raised root `Config` (raised `max_offset_entries` and `max_group_entries_per_instance`), or a delimiter that diverges from the wire. Divergence means a non-conforming frame on a session with validation off, where one slice can span several walk instances.
- D1's budget inside a sub-table is reached by a window cluster built against the sub-table's own mask under the seed hook.
- On each route today mode (c) does not fire, because the error is not `out_of_memory`. The nested getter reads `OK`/0 and the typed view empty and not failed: a silent loss.

**Witnesses** (this is the nested limit witness; D5's nested arm is defensive), each read twice, on the nested getter and the typed nested accessor:
- (i) a test-built `MessageView` with raised root caps over an instance larger than the sub-table's entry cap that carries a nested group, wrapped as `fixpp_msg` as `tests/capi/message_read_failloud_test.cpp` does;
- (ii) D1's window-cluster generator against the sub-table's mask under the seed hook, asserting that the root build is OK and the sub-build fails, on a test-built `table_view` whose group registers the cluster's tags as members in the read's context (more than `kMaxBuildProbe` distinct member tags, so the cluster is wider than `kMaxBuildProbe` slots; no shipped group has that many members).

**Alternative:** have the sub-table inherit the root's `Config` (removes that path, but changes the caps nested reads run under, and D1's budget can still fail a sub-build). Recorded as a lead (spec Out of Scope).

### D8 — C-ABI version (FR-010)

**Decision:** C-ABI **1.15, BREAKING** (`[const §X.7]`). No symbol, struct or code is added. Affected, each marked BREAKING in its documentation:
- `fixpp_msg_get_group` and `fixpp_group_get_nested_group` (`include/fix/c_api/message.h`): the CA-010 block and both functions' docs carry the contract's table and its "Changes from 1.14"; the header block's "Steady-state thunks: abort on any escaping C++ exception" is amended for the two getters, whose cursor-shell `bad_alloc` is now a documented return.
- The session observers whose 1.11 clause says "a frame the session admits always parses for dispatch" (`include/fix/c_api/session.h`): amended in place, marked 1.15, for a frame crafted against the index seed (D1).
- The 1.11 entry and a new 1.15 entry in `include/fix/c_api/version.h`'s history, naming this feature.
- `tools/capi_freeze.sha256` refreshed for `message.h`, `version.h`, `session.h`; `tests/capi/version_test.cpp` to 1.15.

### D9 — Witnesses (SC-001)

Every cell names the requirement it witnesses; each production change has a mutation that turns its cell RED. Every cell also names the construction that reaches its state (a shipped parse, a session, a test-built `MessageView`, with or without the seed hook) and whether a repeat read takes a cache path; where it does, the cell has a repeat twin. A state that no construction reaches is marked defensive with its condition, as contract row 3's malformed-data arm is. `/speckit-tasks` names the construction per cell.

- **#645 (D1).** A window-cluster generator under `set_overlay_seed_for_testing`: with the known seed and the mask of the real entry count (envelope tags included), take the k distinct tags whose home slots are lowest (one window of slots) and insert them highest-home first, drawing only from tags that are not envelope or session-header tags and not standard Length/Data pair tags (so the frame cannot fail on framing or splitting instead); parse fails with `wire_offset_table_full`. Mutation: revert the budget; the frame parses (and, in the session cell, the callback fires), proving the cell measures the budget. A non-crafted frame from the D1 measurement that reaches today's per-insert cap: parses, every tag found (RED today). The existing single-slot cell flips to "found, build OK"; its different-seed sibling stays a control. **Inbound session cell (required):** the crafted frame, under the seed hook set before the session's first parse, closes the session terminally with no Reject and no callback. Every seed-setting cell restores the prior seed by RAII.
- **#644 (D6).** A top-level group whose slice allocation fails (the Q33 null-upstream headroom rig), read through `MessageView::group<>()`: `alloc_failed()` and `read_failed()` true. `InboundFrameDispositionsQ33.GroupSlicesAtHeadroomExhaustion` calls the span wrapper and stays a control; it is not this witness. Flat getter `WIRE_LIMIT_EXCEEDED`; the Q33 flat cell flips.
- **#541 (D4).** Port `b28-repro`'s `group_cursor_shell_arena_exhaustion_capi_test.cpp` (`e10c8b80`): each claim cell asserts `last_rc == FIXPP_ERR_WIRE_LIMIT_EXCEEDED` (the port's `LoopEnd::by_error` alone accepts any non-OK); its controls stay GREEN.
- **#647 (D2, D3).** Flat: registered zero count mid-body and as the last body field; lying count; 802 at the root with value 0 and with `n`; NoPartyIDs(453) with `n ≥ 1` in a declared message that does not declare Parties, and a body group with `n ≥ 1` in a MsgType the dictionary does not declare (US1.9, both RED today); a header group in an undeclared MsgType (US1.10, a control today, RED under D2 without the section arm); tag 10 (control). Nested: zero count mid-instance; present scalar mid-instance **and** last in instance; absent tag. Typed: the same frames. Bare-only registration: NoHops on a route table, and the B-384-2 fixture (controls). SC-004: a validator-and-read agreement cell on one frame registered in its exact context. Mutation: replace the predicate with today's next-field membership; the scalar-last and lying-count cells go RED.
- **Section arm (D2, #647; Gate A round 3 hand-edit, owner, 2026-11-07).** Cells on a loaded custom XML. Most are controls on `fa778a8e`, because today's bare fallback already gives the target ending; round 2's header arm (`parent_path` empty ∧ `is_header_tag` ∧ `group_bit`), or the mutant named for each, is what would turn them RED.

  | Cell | Today (`fa778a8e`) | Target | Kind |
  |---|---|---|---|
  | (i-a) `kScalarReuseXml` with `NoGood` moved into `<header>`; 600=`n`, then `610`, read in `V2` and in an undeclared type | phantom cursor | `TYPE_MISMATCH` | RED on `fa778a8e` |
  | (i-b) the same, 600=0 | `TYPE_MISMATCH` | `TYPE_MISMATCH` | control; RED under round-2 D2 (`OK`/0) |
  | (i-c) the same, 600=`n`, then a non-member | `TYPE_MISMATCH` | `TYPE_MISMATCH` | control; RED under round-2 D2 (`OK`/0) |
  | (ii) a `<trailer><group>`, n ≥ 1, read in an undeclared type | cursor | cursor | control; RED under round-2 D2 (`TYPE_MISMATCH`) |
  | (ii-0) the same with count 0 | `TYPE_MISMATCH` | `OK`/0 | RED on `fa778a8e` (the general #647 zero-count change) |
  | (iii) a group nested in a header group, n ≥ 1, read in an undeclared type | cursor | cursor | control; RED under round-2 D2 |

  Further controls: US1.10's FIX44 `NoHops`, and the same trailer group in its declared message. Mutants, each turning the named cells RED:
  - restore `parent_path.empty() ∧ is_header_tag ∧ group_bit`: (i-b) and (i-c) go RED;
  - drop the trailer walk: (ii) goes RED;
  - record root groups only, without their path: (iii) goes RED.
- **Repeat reads (D3, US1.11).** A twin of each zero-count cell above that reads twice: at the wire layer (`group_slices_status` twice, and `nested_group_slices` twice), on the typed path (`MessageView::group<>()` twice, and the nested accessor twice), and on both getters. Both reads report a group, `OK`, zero instances. A repeated scalar control (read twice, not a group both times) catches a mutant that sets `is_group = true` everywhere. Mutation: push the row with `is_group = false`; the repeat cells go RED while the first-read cells stay GREEN.
- **D5.** Top level: an instance past the per-instance cap, read twice: typed `read_failed()` true and `alloc_failed()` false; flat getter `WIRE_LIMIT_EXCEEDED` both times. Nested `wire_group_too_large`: defensive (D5's condition); the RED phase records it as unreachable.
- **D7.** The two constructions in D7's witnesses: nested getter `WIRE_LIMIT_EXCEEDED` and typed `read_failed()`, first and repeat read. Mutation: delete D7's widening.
- **D10.** A present scalar read on an exhausted arena: `TYPE_MISMATCH` on both getters (nested RED today: `alloc_failed` from the sub-table or the cache push; flat a control, since it drops the flag). At the wire layer, the same read through `group_slices_status` reports `status == ok`, `!is_group` (RED today: `alloc_failed`). Repeat reads for every failure origin (slices, cache row, shell, limit) return the first read's code. Cache row: a registered group with n ≥ 1 whose row push fails reads `alloc_failed`, and so does its repeat; one whose push fits and whose slices fail leaves no row, so its repeat recomputes and fails the same way. Nested: a present, registered group whose `nested_cache_` push fails reads `alloc_failed` on the first read and on the repeat. Construction: the null-upstream rig, with the arena's remaining room stepped across the push and the following allocation; a lane where no step separates the two records the cell as skipped. Mutations: move the flat push after the slice allocation, and separately the nested push after the build; in each, some step yields a first read that completes and a repeat that fails, and the "repeat answers as the first" assertion goes RED.
- **D11.** An absent nested tag on an exhausted arena: C-ABI `TAG_NOT_FOUND`, typed `!read_failed()`. A present scalar in an instance, under each failure origin where no usable sub-table exists: mode (a) (no sub-table shell), mode (c) (sub-build out of memory) and D7 (a sub-table over its entry cap). For each, the wire layer reports `ok` and `!is_group`, and the nested getter `TYPE_MISMATCH`. The typed half is covered at the wire layer, because generated nested accessors exist only for registered groups. Mutations: delete the presence scan (the absent-tag cells go RED); separately, delete the registration check (the present-scalar cells go RED while the absent-tag cells stay GREEN, which proves the two arms are told apart).
- **Context-key bitset (D2).** A `table_view` fixture that registers a tag only through `add_group_required_member_ctx` under one context: registered there, not registered under a second context. Mutation: drop the bit-set in that setter; arm 3 then fires and the second-context cell goes RED.
- **Row 0.** The nested getter with a null `g` and both outputs non-null: `NULL_HANDLE`, outputs null/0 (RED today); either getter with one out-pointer null: `NULL_HANDLE`, the other output untouched (control).
- **D2 regression sweep (FR-009).** Over every shipped dictionary and the route tables:
  - every group a message places at its top level (body, header, trailer) satisfies the registration predicate in `(msg_type, [])`, and every nested group satisfies it under its parent path;
  - per dictionary, one synthetic undeclared MsgType: every header and trailer group is registered, at every depth, and no body group is registered there unless it is bare-only (arm 3);
  - the sweep reports every tag for which arm 2 or arm 3 fires. Its oracle is QuickFIX's lookup keys: for arm 2, the section store equals the header and trailer group declarations re-derived independently from each shipped dictionary's XML or Orchestra source; arm 3 only for a registration this bundle's survey names as bare-only (099's NoHops on route tables). Any other tag is a RED-phase finding to record, not a cell to adjust.

  *(Round 2's fourth bullet, the census re-reads of arm 2's conditions, is deleted with those conditions: superseded by Gate A round 3 hand-edit (owner, 2026-11-07).)*

  A header-group control on a non-route table (for example a header group of a FIX44 message read through the flat getter) stays a cursor. Prove the sweep can fail before trusting it: delete the predicate's bare arm; separately key it on a wrong parent path; separately delete the section arm (the undeclared-MsgType arm goes RED). Each mutant turns the sweep RED.
- Lanes: the arena-exhaustion cells assert only under a null arena upstream; on MSVC debug they skip. The verify record names, per cell, the lanes that show RED and those that skip.

### D10 — The group cache records only completed reads (FR-007)

This is the one statement of the cache rule; the data model points here.

**Decision, flat cache (`group_index_` in `group_slices_status`).** A row is cached only for a completed read, and a failed read leaves no row.
- **A registered group with n ≥ 1 instances.** The count pass has computed n without allocating, so the row is pushed after the count pass and before the `allocate()`. If the push throws, the read fails `alloc_failed`; on a monotonic resource every repeat fails the same way. If the slice allocation then throws, the row is popped (`pop_back`), so the failed read caches nothing.
- **An answer that needs no slices** (absent, not a group, zero instances read). The push has its own `try`. If it throws, the answer is served uncached: recomputing it allocates nothing and yields the same answer.
- **A limit failure** (`limit_exceeded`) pushes nothing. `group()` allocates nothing, so the failure recurs deterministically.

**Decision, nested cache (`nested_cache_` in `nested_group_slices`).**
- When the call builds a sub-table, its row is pushed **before** the build. A failed push is a failed read, resolved as a null sub-table is (mode (a), through D11's failure rule). The built table, or null, is stored in the pushed row.
- When the call reuses the sub-table of an earlier row for the same slice and bundle, a failed push serves the answer uncached. The repeat finds the same row and the same table.
- A cached row is re-resolved at the cache-hit exit through `resolve_nested_result` (D11), whatever the row holds (a null sub-table, a failed build, a built table). So a repeat answers as the first read did.

So a completed read's repeat fails only where the getter allocates a cursor shell (FR-007).

**Alternatives:**
- Cache the status in `group_span` too and replay it. Workable within 16 bytes, but caching an outcome is what made the first draft's repeat read differ from the first; recomputing has one source of truth. Rejected.
- Push after the slices and serve a materialized result uncached when the push fails (this bundle's round-1 text): the slice bytes stay consumed on the monotonic arena, so the repeat allocates again and can fail. One condition, two endings. Rejected; the same holds for building a sub-table before its row.
- Reserve machinery (a pre-reserved row slot per read): more state than push-then-pop for the same guarantee. Rejected.

### D11 — On the nested path, a failed read means a present, registered tag (FR-004, 073 FR-007)

**Decision:** `resolve_nested_result` receives the instance slice, the call's `ctx` and the caller's `hooks` (those of the `nested_group_slices` call), at both of its exits: the cache-hit exit and the final exit. When any failure mode fires (a, c, D7, or a failed nested cache push (D10)), it decides in this order:
1. **Registration first, because it is the cheaper test.** Ask D2's predicate for `(ctx, nested_no_tag)` through `hooks`' registration callback; it is O(1) and allocates nothing. Not registered → `{empty, ok, !is_group}`. The nested getter's existing presence probe then separates `TAG_NOT_FOUND` from `TYPE_MISMATCH`, so the wire layer needs no scan on this branch.
2. **Presence.** Scan the slice for `nested_no_tag` without allocating: a field walk that splits Length+Data through `hooks.data_tag_for_length`, the rule `OffsetTable::build` uses. Absent → `{empty, ok, !is_group}`.
3. **Present and registered** → the failed status.

It never uses `this->hooks_`, for the registration callback as for the scan: pairing the root table's own bundle with the caller's split would reintroduce the mismatched-pairing defect fixpp#426 closed. (`scan_slice_for_tag` is local to `src/capi/message_read.cpp` and uses `parser.hpp`'s `field_iterator`, which `offset_table.hpp` cannot include, so the wire layer has its own walk.)

Mode (b) needs no extra step: inside a built sub-table, `group()` asks registration before it allocates (D2), and D10's push rule keeps a not-a-group answer `ok`. So the invariant in data-model `nested_slices_result` holds on every exit.

**Rationale:** costs nothing on the success path; meets 073 FR-007 and FR-004 without qualification; the typed table holds as written; and the C getters' one ordered table follows.

**Alternative:** decide presence in the C getter before calling the wire layer: fixes the C ABI only, and the typed nested accessor would still report `alloc_failed()` for an absent group. Rejected.
