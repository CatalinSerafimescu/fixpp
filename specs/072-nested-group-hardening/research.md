# Phase 0 Research: 072-nested-group-hardening

**Feature**: Harden doubly-nested repeating-group correctness (bundles #180 census+guard / #183 typed depth-≥2 context).
**Method**: two CodeGraph-backed source explorations + advisor adversarial pass. All anchors below quoted/verified against on-disk source at branch `072-nested-group-hardening`. Where the GitHub issue text diverges from source, the source wins and the divergence is recorded.

---

## Part A — census pins + load-time delimiter guard (#180)

### D-A1. Guard seam = `XmlLoader::load_*`, new typed error thrown at load
**Decision**: Detect the nested==parent delimiter collision inside the loader post-parse (`LoaderState::finalize()`, `src/dictionary/xml_loader.cpp:633`, after `groups_` is populated), throw a **new distinct typed error** deriving from `dict::xml_parse_error` (so existing `catch(dict::xml_parse_error&)` bad-dictionary handlers still catch it) with its **own `code()`** returning a new appended `fixpp::core::error` variant (so specific handling is possible). It propagates unchanged out of the `trap_throw_or_throw<xml_oom_error>` window (that helper rethrows non-`bad_alloc` unchanged — `decimal_helpers.hpp:47`).
**Rationale**: [Clarification 2026-07-12] load path already throws typed catchable errors; `as_table_view()` returns by value with no error channel and runs at session setup. Deriving *from* `xml_parse_error` (not a bare sibling) keeps backward-compat for callers that already treat bad-dictionary content uniformly.
**Alternatives**: throw in `as_table_view()` (rejected — hot session-setup path, needs new throw channel); return-based status (rejected — inconsistent with the load convention); bare `std::runtime_error` sibling (rejected — not caught by existing `xml_parse_error` handlers).
**Anchors**: `include/fixpp/dict/error.hpp` (`xml_parse_error:33`, `unknown_version_error:47`, `xml_oom_error:64`); `src/dictionary/xml_loader.cpp` (`load:889`, `load_from_string:906`, `build_handle_from_doc:879`, `finalize:633`); `include/fixpp/core/error.hpp` (append-only enum).

### D-A2. Guard walks the structural loader group table (`groups_`), covers all 9 dicts
**Decision**: For each `GroupDef` in `LoaderState::groups_` with `parent_group_no_tag != 0`, look up the parent via `group_index_by_no_tag_` and compare `first_field_tag`. `GroupDef` (`xml_loader.cpp:207`) carries both `first_field_tag` (delimiter) and `parent_group_no_tag`, set in `expand_field_list` (`:519-526`) **after** component expansion — so the delimiters are post-expansion. This structural table registers a group per raw `<group>` element regardless of whether the count field is typed `NumInGroup` or `INT`.
**Rationale**: uniform coverage including FIX40/41/42 (see D-A4).
**Residual (recorded, FR-005b)**: `groups_` is deduplicated **globally first-seen-by-`no_tag`** (`expand_field_list:486` `if (!group_index_by_no_tag_.contains(no_tag))`), so the guard checks the first-seen parent/delimiter per `no_tag`, not per-message-context. A collision that appears only in a non-first-seen context of a reused `no_tag` is unguarded (same L-063-3 first-seen-context class). Acceptable: no shipped dict exhibits it.

### D-A3. Census assertions belong in `reused_tag_census_test.cpp`, derived to avoid vacuous pass
**Decision**: Add two permanent assertions to `tests/dictionary/reused_tag_census_test.cpp` (test `AllNineRuntimeDictsCensused`, per-dict loop). **FR-001 (delimiter disjointness)** derived from the same structural `groups_`/`GroupRef` walk as the guard → uniform 9-dict coverage. **FR-002 (scalar-member disjointness)** derived from per-group member sets. Each assertion asserts **non-vacuous coverage** (observed > 0 groups for each dict it claims to cover), mirroring the existing hard invariant `EXPECT_TRUE(saw_fix44_295_collision)` (`:281`).
**Anchors**: `kRuntimeDicts` (`reused_tag_census.hpp:130-133`, all 9 XMLs); `census_for` (`:61`, mirrors `as_table_view` `immediate_parent`/member-set/path walk); `DelimiterScan`/`walk_groups`/`delimiter_scan_for` helpers already present in the test file.

### D-A4. FIX40/41/42 false-green risk — the reason FR-001 uses the structural walk
**Divergence / hazard**: `as_table_view()` and `census_for` identify groups via `fr.type == NumInGroup` (`dictionary.cpp:335,368`), but **FIX40/41/42 declare group count fields as `INT`, not `NUMINGROUP`** — so `census_for` registers **zero** groups for them (the in-test "REGISTRATION GAP" escalation note, `reused_tag_census_test.cpp:153-194`). A census built naïvely on `census_for` membership passes **vacuously** for those three. **Mitigation**: FR-001 uses the structural `groups_` walk (count-field-type-agnostic) + non-vacuous assertion. **FR-002 residual (recorded, FR-013d)**: scalar-member disjointness is inherently membership-based; if per-group member sets can't be recovered structurally for FIX40/41/42, those three are an **explicit unpinned residual**, not counted as covered.

### D-A5. The mis-split the guard prevents
**Anchor**: `OffsetTable::group_slices()` flat splitter loop `src/wire/offset_table.cpp:646-668` (function starts `:611`) splits on every reappearance of `entries_[first].tag` (`delim`). Equal parent/child delimiters make a nested delimiter read as a new outer-instance boundary → parent group mis-split. Most exposed on the dict-free fallback (`group()` `:554-557` sets `group_end = entries_.size()`). The L-062-3 scalar hazard: `OffsetTable::find` (`:373`) and `wire::get` (`parser.hpp:548-560`) return the **first** wire occurrence of a tag regardless of context.
**Divergence**: issue #180 cites `group_slices` at "~548-558, 596-599"; actual splitter loop is `646-668` (548-557 is `group()`, 592-601 is `group_slices_reserve_bound()`). Cosmetic — the mechanism is unchanged.

---

## Part B — typed depth-≥2 pushed context (#183)

### D-B1. Fix site = emitter view-mint, push once; call-arg and `operator[]` unchanged
**Decision**: At `tools/codegen/fixpp-codegen/emit_messages.cpp:270-271` the emitter emits `group_view<G_c>{nested, ctx_}` — re-wrapping the parent context verbatim. Change it to emit the returned view's base context with `group_ctx = ctx_.group_ctx.pushed(<c>)` where `c` (the nested group's own `no_tag`) is **already in scope** (emitted as the `nested_group_slices` arg at `:265`). Leave unchanged: (i) the `nested_group_slices(...)` **call argument** stays `ctx_.group_ctx` — the parent's own path is the correct slicing context for the immediate child, identical to the C-ABI (`message_read.cpp:490`); (ii) `group_view::operator[]` (`group_view.hpp:143-150`) stays a verbatim `base_ctx_` copy — pushing there too would **double-push**; (iii) the depth-1 seed `parser.hpp:309` (`ctx.group_ctx = root_ctx.pushed(NoTag)`) is already correct.
**Rationale**: push exactly once per descent at cursor/view **mint**, matching every existing correct path.
**Divergence from issue #183**: the issue says "emitter *and* `group_view::operator[]` push" and frames the call-arg as the bug. Source shows the call-arg is correct; the bug is the returned view's stored context; and pushing in `operator[]` too would double-push. **Corrected in spec FR-007.**

### D-B2. Minimum reproducing depth is depth-3, not depth-2
**Decision/finding**: depth-1 is pushed correctly; passing the parent path to `nested_group_slices` slices the immediate child (depth-2) correctly; scalar member reads use a first-occurrence wire scan (no membership context). The frozen path first mis-slices a **grandchild (depth-3)** group. Concretely: inside a depth-2 group the stored context stays `[parent]` instead of `[parent,child]`, so slicing the grandchild queries `(mt,[parent],grandchild)` → miss → bare fallback.
**Divergence from issue #183**: the issue frames a "depth-2 read of member X from a 539-entry" as the bug. That read doesn't exercise it. **Corrected in spec US2/FR-011/SC-003/Edge Cases** to depth-3. Verified against `parser.hpp:277-311`, `offset_table.cpp:718-721` (`nested_group_slices` seeds sub-table ctx verbatim), `consume_group_extent:466` (the wire slicer *does* push per level — only the typed accessor's cross-call threading is broken).

### D-B3. Reconciliation target = C-ABI (already correct); do not touch it
**Anchor**: `src/capi/message_read.cpp` — top-level seed `:379` (`grp->group_ctx = offsets.group_context_for(group_tag)` = `stored.pushed(no_tag)`), nested mint `:506` (`nested_grp->group_ctx = parent_grp->group_ctx.pushed(nested_tag)`). Typed fix must make the child view's stored `group_ctx` equal `ctx_.group_ctx.pushed(c)` — the exact analogue of `:506`. **Never un-push the C-ABI.**

### D-B4. Validator flat-context residual (L-063-3) — largest sub-task
**Anchor**: `include/fixpp/wire/validator.hpp` Step-3 group walk (`:177-276`) hardcodes `root_path = {}` (`:204`) — every nested-group membership lookup queries the root context and misses (one level worse than the accessor, which at least pushes once). It uses a pre-063 `seen_in_instance` heuristic (`:258-267`), not `consume_group_extent`. **Decision**: FR-010 makes the validator walk nesting-aware with a pushed context. This is the single largest sub-task and is scoped/verified in its own right (own tasks + witness).

### D-B5. No checked-in flyweights — verify via clean reconfigure, not golden diff
**Finding**: generated flyweights are **build-tree only** (`cmake/Codegen.cmake:5-10`, `${CMAKE_BINARY_DIR}/_codegen/…`; source tree never written). Generation runs at CMake **configure** time (`execute_process`, `:340-359`), gated by the emitter-source SHA256 fingerprint (`:274-276`) + tool-binary mtime marker (`:308-315`). A change to `tools/codegen/fixpp-codegen/*` forces a re-run on next configure. **There is no checked-in golden and no golden-diff test** — the behavioral gate is recompiling the typed-read tests against regenerated headers.
**Decision (SC-005)**: verify with a **clean reconfigure** (delete `_codegen/`) across at least debug + sanitizer + coverage configs, to avoid a stale `_codegen/` header being compiled (the `project_codegen_emitter_staleness` false-green/red hazard). **Divergence from spec's first draft** ("forced golden regen … checked-in generated source"): corrected — no git-visible churn exists.

### D-B6. Witness = real generated type + hand-built `table_view` (codegen wall)
**Finding**: typed accessors exist only for the four codegen-input dicts (v42/v44/v50sp2/vt11). You **cannot** XML-load an arbitrary dialect and get typed reads. So the discrimination witness uses the real `v44::MassQuote` type (`quote_sets(296)→quote_entries(295)→legs(555)`, a shipped depth-3 chain) driven by a **hand-built `table_view`** whose grandchild group `555` has a *different* member set in the context store `add_group_member_ctx(MassQuote,[296,295],555,…)` vs the legacy bare `add_group_member(555,…)` store. Pre-fix the frozen `[296]` read hits the wrong bare set; post-fix the pushed `[296,295]` read hits the right context set. Extends the existing mutation-proven depth-2 pattern `RealDictionaryMassQuote296RootContextSeededAtCtorNoCachePoison` (`tests/codegen/nested_group_read_test.cpp:613-690`) one level deeper.
**Consequence**: the "witness dialect must satisfy the guard" coupling in the first spec draft is **false** — a hand-built `table_view` bypasses `XmlLoader::load_*`. Part A and Part B have **independent** fixtures. A 065-style production loopback witness likely **cannot** be built for Part B (real dicts inert, typed layer codegen-bound). See [[feedback_typed_nested_read_witness_codegen_bound_to_shipped_dicts]]. **Corrected in spec Edge Cases / FR-011.**
**Template**: `tests/codegen/nested_group_read_test.cpp` (`make_frame:86`, `parse_decimal:102`, MassQuote read chain); registration forms `table_view::add_group_member` (bare, `:316`) vs `add_group_member_ctx` (`:359`); context→bare fallback at `group_first_field:268` / `group_member_tags:278`.

---

## Cross-cutting

- **C-ABI frozen** (`1.5.0`): Part B touches only C++ typed path + validator + census; Part A touches only loader + census. No exported C symbol / header / enum / version change (FR-012 / SC-006). ABI hygiene gate must report no delta.
- **New `fixpp::core::error` variant** for the delimiter-collision `code()` — appended at an unused slot (Article X §4 append-only rule); this is a C++ `dict::` error, not a C-ABI `fixpp_error_t` change, so it does not touch the frozen C-ABI enum.
- **Constitution triggers**: wire/codegen/error-semantics ⇒ Gate A mandatory (Art. XVII §1); `/clarify` (done) + `/analyze` mandatory (Art. XVI §3-4). TDD red-green (Art. VII §3). Isolation-safe new tests grouped, selected by `ctest -L` (Art. VII §8). Coverage ≥95/85 touched modules (Art. IX §1). ASan/UBSan/TSan Tier 1 (Art. IX §2).
