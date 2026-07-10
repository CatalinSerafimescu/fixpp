# Research: Membership-aware C-ABI nested repeating-group read (065)

**Feature**: fixes issue #179 / L-063-2. **Date**: 2026-07-08.

This is a bounded, single-defect read-path correctness fix on the GA-frozen (`1.5.0`) C-ABI. No NEEDS CLARIFICATION markers survived `/speckit-specify` or `/speckit-clarify` (the one apparent fork — the degradation contract — resolves by construction, see Decision 3). Research below records the mechanism decisions Gate A will scrutinize.

---

## Decision 1 — Reuse `OffsetTable::nested_group_slices` (062) instead of the hand-rolled positional scanner

**Decision**: Delete the hand-rolled positional Phase-1/Phase-2 delimiter scanner in `fixpp_group_get_nested_group` (`src/capi/message_read.cpp:475-599`) and delegate nested-instance slicing to the existing membership-aware primitive `OffsetTable::nested_group_slices(slice_data, slice_len, nested_no_tag, opaque_dict, group_member_fn, gen, ctx)` (`src/wire/offset_table.cpp:668`).

**Rationale**:
- The defect is entirely in how the **last** nested instance is bounded: the positional scan closes it at the end of the outer entry's slice (`:591-596`), which is membership-free, so a trailing outer member's bytes fall inside it.
- `nested_group_slices` → `build_nested_subview` builds a dict-aware sub-`OffsetTable` over the entry-slice bytes and calls `group_slices(nested_no_tag)` on it, which bounds every instance — including the last — via `consume_group_extent` (`:429`), the same context-threaded, nesting-aware, membership-driven walk that makes the C++ typed read path correct. A tag that is not a member of the nested group (a trailing outer member) ends the last instance's extent (`:469` `break`), so it is excluded by construction.
- This makes the C-ABI and C++ typed paths agree **by sharing the same primitive** (FR-005), not by re-deriving membership — eliminating the divergence class L-063-2 names, and any future drift.
- It is a net **simplification**: ~120 lines of hand-rolled pointer arithmetic (incl. the `LCOV_EXCL` dead-branch block) are deleted and replaced by one primitive call + a small presence probe. This directly answers the "gratuitous rewrite" objection recorded against the *shape* of this change in 063 (see Decision 5): reusing a shared primitive is the opposite of a rewrite.

**Alternatives considered**:
- *Positional heuristics on the existing scanner* (uniform per-instance width; stop-on-first-unseen-tag). Rejected in 063 T025 and again here: unsound for variable-shape groups; "don't invent enforcement." A correct bound genuinely needs membership.
- *New independent membership implementation at the C-ABI cursor.* Rejected: duplicates `consume_group_extent`, invites drift, larger surface. FR-005 forbids it.

---

## Decision 2 — Carry the nested-descent context on the **internal** `fixpp_group` cursor (zero public-ABI)

**Decision**: Add one `fixpp::wire::group_context group_ctx{}` field to `struct fixpp_group` (`src/capi/capi_internal.hpp:291`). Seed it at the two cursor-mint sites:
- `fixpp_msg_get_group(group_tag)`: `grp->group_ctx = view->offsets().group_context_for(group_tag)` — a new public C++ (non-C-ABI) accessor `OffsetTable::group_context_for(std::uint16_t no_tag) const noexcept` returning `stored_group_context().pushed(no_tag)`. `stored_group_context()` is `private` (`offset_table.hpp:257`) so the non-member C-ABI free function `fixpp_msg_get_group` cannot call it directly; `group_context_for` is DECLARED in `offset_table.hpp` and DEFINED OUT-OF-LINE in `src/wire/offset_table.cpp` (the `.pushed()` body needs the complete `group_context` type, only forward-declared in the header — same out-of-line rule as `stored_group_context()`).
- `fixpp_group_get_nested_group(outer, i, nested_tag)`: pass `outer->group_ctx` as the `ctx` to `nested_group_slices` (the context under which `nested_tag`'s members are registered at depth-1), and set the returned nested cursor's `nested->group_ctx = outer->group_ctx.pushed(nested_tag)` (the arithmetically-correct full path — see Decision 7 for the depth-scope reasoning).

**Rationale**:
- `nested_group_slices` needs the membership context under which the nested group's members are registered — for a nested group `N` under outer group `O`, that is `{msg_type, parent_path=[…, O]}`. The typed path threads exactly this as `entry_context::group_ctx`, computed as *the entry's container path pushed with the entry's own no_tag* (`group_view.hpp:56` `pushed()`; emitter `emit_messages.cpp:263-268` passes `ctx_.group_ctx`). The C-ABI cursor is the only place that knows "which group am I / what path led here," so it must carry the same context. This is fully **symmetric** with the typed path.
- `struct fixpp_group` is declared in the **internal** `capi_internal.hpp` and only *forward-declared* in the public header (`include/fix/c_api/message.h:61` `typedef struct fixpp_group fixpp_group_t;`). Consumers hold it only as an opaque `const fixpp_group_t*`. Adding a member changes **no** exported C symbol, no public C header, no C error enum, no ABI version — SC-003 / FR-006 hold. `group_context` is trivially-copyable (`static_assert` `group_view.hpp:66`); the field is value-stored (arena-allocated cursor, no lifetime concern).
- **Only `group_ctx` is needed — not a separate `no_tag` field** (the shape 063 rejected carried both). The descent passes the cursor's `group_ctx` directly as `ctx`, matching the typed path's single-field threading; the nested no_tag is the call argument, and the child cursor's context is `group_ctx.pushed(nested_tag)`. This is strictly smaller than the rejected 063 shape.

**Alternatives considered**:
- *Public accessors on `OffsetTable` for `opaque_dict_`/`group_member_fn_`/`gen_`* so the C-ABI can call the full `nested_group_slices` overload directly. Rejected in favor of a convenience overload (Decision 4) — accessors leak private wiring; the overload encapsulates it.
- *Reconstruct context inside `OffsetTable` from its stored root context.* Impossible: the root table does not know which cursor the caller is descending from; the path must live on the cursor.

---

## Decision 3 — Degradation for a membership-unavailable cursor is correct **by construction** (resolves the only clarify fork)

**Decision**: No separate fallback path. A cursor whose owning `OffsetTable` has a null `group_member_fn_` (dict-free parse) automatically degrades to today's positional behavior, because `build_nested_subview` builds the sub-`OffsetTable` with the null predicate and its `group()` takes the dict-free fallback (`offset_table.cpp:545-547`, `group_end = entries_.size()`), yielding the same "last instance runs to slice end" slicing as the current scanner.

**Rationale**:
- Inbound C-ABI messages are engine-parsed with a dictionary **as of 066** (PR #181): `Session::parse_and_dispatch_` (`session.cpp:316`) now builds `Parser{*inbound_tv_}` from `cfg_.dictionary->as_table_view()`, so `stored_group_context()` is non-empty on the dispatched C-ABI view (confirmed by the `GroupMembershipCapiRed.TrailingFieldAbsentFromLastInstance` loopback test). *Before 066 this path was dictionary-free — the premise error that parked 065; this bullet's "always engine-parsed with a dictionary" claim was aspirational-then-false and is now true.* A genuinely dict-free cursor reaching a nested read is now test-harness-only. FR-008 requires "no crash, no UB, no regression versus today" — reproducing today's behavior exactly satisfies it with **zero** new code. This is why `/speckit-clarify` surfaced no user question: the fork has one safe answer forced by the reused primitive.

**Alternatives considered**: returning empty/`TAG_NOT_FOUND` for dict-free nested reads — rejected, it would *regress* dict-free multi-entry nested reads that work today.

---

## Decision 4 — One convenience overload on `OffsetTable` (public C++, non-ABI)

**Decision**: Add an overload
`std::span<group_slice const> nested_group_slices(std::byte const* slice_data, std::size_t slice_len, std::uint16_t nested_no_tag, group_context const& ctx) const noexcept`
that forwards to the existing 7-arg overload using the table's own `opaque_dict_`/`group_member_fn_` and a **build-mode-safe generation token**.

**Build-mode correctness (must compile in BOTH debug and release)**: `gen_` is a member only `#ifndef NDEBUG` (`offset_table.hpp:262-264`); a body that forwards `gen_` unconditionally fails to compile in release (`NDEBUG`), and no release-safe `token()` accessor exists on `OffsetTable`. Resolution: add a private helper `detail::generation_token token_for_nested_cache() const noexcept` defined in **both** build modes (`#ifndef NDEBUG return gen_; #else return {};`) and have the overload forward `token_for_nested_cache()` — so the overload may stay inline and compiles unconditionally. (Equivalently, the overload could be defined out-of-line in `offset_table.cpp`, passing `gen_` under `#ifndef NDEBUG` and `{}` in release; the helper is preferred because it keeps the token policy in one place.) This mirrors how the existing 7-arg path obtains its token in release: it takes `gen` as a parameter (the typed caller sources it from `MessageView::token()` into `entry_context::gen`, `parser.hpp:297`, `{}`-default in release), i.e. the debug-only generation trap is a no-op token in release, never a compile-time `gen_` reference.

**Rationale**: The root `OffsetTable` (reached via `parent_view->offsets()`) already stores `opaque_dict_`, `group_member_fn_` (and, in debug, `gen_`) as private members; the 7-arg overload only takes them as params because the 062 *typed* caller carries them in `entry_context`. The C-ABI caller has the table itself, so an overload that reads the table's own members is the minimal, encapsulated seam — no private-member accessors. This is a **public C++** surface addition (header + static lib, recompiled), **not** a C-ABI change; the same non-ABI category 063 used for the `group_member_fn_t` context-param widening.

**Alternative**: expose `opaque_dict()`/`group_member_fn()`/`gen()` accessors — rejected (leaks internals; three symbols vs one).

---

## Decision 5 — Reversing 063 plan.md Round-2's `fixpp_group` cursor-field rejection is sanctioned, not a contradiction

**Decision**: This feature adopts the internal-cursor `group_context` field that 063 Gate A Round-2 recorded as *rejected* (063 `plan.md:95`). This is **correct and pre-authorized**, not a re-litigation.

**Rationale** (Gate A will check this — stated plainly to preempt a false "contradicts inherited decision" finding, cf. `feedback_clarify_reconciled_away_standing_user_decision`):
1. **063 explicitly deferred this exact fix to a dedicated follow-up feature.** 063 `plan.md:106` (T025 disposition): *"Disposition: documented limitation `L-063-2` + membership-aware C-ABI follow-up feature; witness kept as `GTEST_SKIP` (un-skipped by the follow-up, `:353` lifecycle)."* This feature **is** that follow-up.
2. **The rejection was scoped to 063's job.** 063's mandate was the loader/extent fix; routing the C-ABI cursor rewrite through it was correctly judged out-of-scope ("gratuitously rewrite a GA-frozen-ABI internal *the [063] fix does not require*"). A dedicated feature with its own spec + Gate A is the sanctioned vehicle 063 named.
3. **The "gratuitous rewrite" objection is answered by the reuse mechanism (Decision 1).** The change is a net *deletion* of the hand-rolled scanner in favor of a shared primitive — the opposite of a rewrite. The one added field is on an internal struct (zero public ABI).
4. **The reachability re-assessment changed the priority.** The 2026-07-08 Fable audit (issue #179) reclassified L-063-2 from "edge-activated" to a ubiquitous, any-counterparty-reachable **silent wrong value** (222 enabling layouts in FIX44, ~20k in FIX50SP2) — the worst failure class for a wire library — which justifies spending a dedicated feature now.

**Alternatives considered**: leave L-063-2 as a permanent documented limitation. Rejected on reachability grounds (Rationale 4) and because a correct, low-surface fix now exists via primitive reuse.

---

## Decision 6 — Preserve the absent-vs-empty-count error distinction with a membership-free presence probe

**Decision**: After obtaining `slices = nested_group_slices(...)`:
- `!slices.empty()` → `FIXPP_ERR_OK`, `*nested_count_out = slices.size()`.
- `slices.empty()` → probe the outer entry slice for the presence of `nested_tag` (a membership-free byte scan, the existing `scan_slice_for_tag` idiom): **present** → empty group → `FIXPP_ERR_OK`, `nc=0`; **absent** → `FIXPP_ERR_TAG_NOT_FOUND`.

**Rationale**: `nested_group_slices` collapses both "nested_tag absent" and "nested_tag present but count 0 / no delimiter" to an empty span, but the current C-ABI contract distinguishes them — pinned by `NestedGroupAbsentTag` (`:1420` → `TAG_NOT_FOUND`) and `NestedGroupEmptyGroupCountLastField` (`:1691` → `OK`, nc=0). The presence probe replicates the current Phase-1 `found_count` branch exactly (`message_read.cpp:524` vs `:527`), preserving both tests. The probe is cheap (one linear scan of the entry slice) and only runs on the empty-result path.

**Scope of the "empty → OK, nc=0" mapping**: this is a **membership-free** slice scan for the *successful* empty cases only — nested_tag absent (→ `TAG_NOT_FOUND`) vs present-with-count-0 (→ `OK, nc=0`). It maps **no error to OK**. `group_slices()` also returns an empty span on a collapsed `err_group_too_large` (the lossy channel corrected in the FR-009 / "depth/entry caps" note), but that case is **unreachable** on the public C-ABI nested read (over-limit fails the outer read first; the consumer never obtains this cursor). The probe's empty→OK mapping is therefore safe **only because of that unreachability argument** — it would be a silent-truncation bug if the over-limit case could reach here.

**Alternative**: return the sub-`OffsetTable` handle from `nested_group_slices` to call `find(nested_tag)` — rejected (widens the primitive's signature for one caller; the slice scan is simpler and already an established helper).

---

## Decision 7 — Scope to depth-1; store the correct pushed path; surface (do not fix) a pre-existing typed-path depth-2 gap

**Decision**: This feature delivers the **depth-1** case (issue #179: a trailing outer member after a *single* nested group). On the nested cursor store `group_ctx = parent->group_ctx.pushed(nested_tag)` — the arithmetically-correct full parent path. Depth-≥2 nested-in-nested *read correctness* is out of scope and not witnessed; SC-005 equivalence is asserted only for the depth-1 layout.

**Context-keying facts verified** (`src/dictionary/dictionary.cpp:394-433`, `include/fixpp/wire/parser.hpp:291-300,528-540`):
- `as_table_view()` registers a group's members under `(msg_type, FULL parent-no_tag path excluding the group's own no_tag, no_tag)`. So `453`→`(mt,[],453)`; `539` nested in `453`→`(mt,[453],539)`; a depth-2 `X` in `539`→`(mt,[453,539],X)`.
- `MessageView::group<NoTag>()` seeds the entry context `ctx.group_ctx = root_ctx.pushed(NoTag)` (the level-1 push). For the outer C-ABI cursor, `stored_group_context().pushed(group_tag)` reproduces this exactly.
- At depth-1 the descent context is `{msg_type,[453]}` = `outer->group_ctx`, which matches `539`'s registration key `(mt,[453],539)` — so the C-ABI read of `539` (and its trailing-member exclusion) is correct, and equals the typed path.

**The surfaced gap (NOT fixed here)**: the generated typed nested accessor (`emit_messages.cpp:263-268`) threads `ctx_.group_ctx` **unchanged** into `nested_group_slices` on a nested descent (no `.pushed(nested_tag)`), and `group_view::operator[]` copies the base context without pushing. So a **depth-2** typed read of `X` from a `539`-entry threads `{msg_type,[453]}` and queries membership under `[453]` for a group registered under `[453,539]` — a **miss**, degrading to the legacy bare-`no_tag` store (Defect-A-prone). This is **pre-existing** (062/063), independent of this feature, and reachable only for genuinely doubly-nested reads. Fixing it requires an emitter change (`.pushed()` on nested descent) + a forced golden regen + a validator counterpart — feature-sized and out of #179's depth-1 scope. **Disposition: record as a new limitation (candidate `L-065-1`) + a follow-up; do NOT fix here.**

**Why store the pushed (correct) path on the C-ABI nested cursor anyway** (rather than mirror the typed path's unpushed threading): (1) it is unused at depth-1, so it cannot affect the witness or any existing test; (2) storing the correct path does not *propagate* the typed path's bug into the C-ABI (the project's "don't enshrine bugs" discipline); (3) a depth-2 C-ABI descent then resolves membership under the correct `[453,539]` key — a strict improvement over the old positional scan, and it does not depend on the typed path.

**Candidate `L-065-1` (record explicitly, do NOT fix here): a depth-2 C-ABI-vs-typed divergence.** 065 stores `nested->group_ctx = parent->group_ctx.pushed(nested_tag)` on the C-ABI nested cursor (the correct full path), whereas the emitter threads the **unpushed** context on nested descent (`emit_messages.cpp:262-271`). So at depth-2 the C-ABI resolves membership under the correct full path `[453,539]` while the typed path resolves under the too-short `[453]` — a genuine C-ABI-vs-typed divergence (the exact class 065 exists to eliminate), the C-ABI being the more-correct side. This divergence is **INERT at depth-1** (065's scope — verified: the nested cursor's `group_ctx` is read only on a *further* descent; depth-1 field reads go through `scan_slice_for_tag`, `message_read.cpp:141`, which never touches `group_ctx`) and is explicitly bounded out of SC-005. It is a consequence of the pre-existing typed gap, not introduced here; recorded as candidate limitation **L-065-1** so the reconciliation obligation is on the record when the typed follow-up lands (which must push the typed context, not un-push the C-ABI's).

## Cross-cutting facts verified during research

- **Context symmetry confirmed** against the emitter call site (`emit_messages.cpp:263-268`): typed descent passes `ctx_.group_ctx` (= container-path pushed with own no_tag) as `nested_group_slices`'s `ctx`. The C-ABI seeds the identical value on the cursor.
- **Root context has msg_type** (063 Gate-B RC#1 fix `a5e0b310` seeds root `group_context` in the dict-aware `MessageView` ctor), so `stored_group_context()` returns `{msg_type=<actual>, path=[]}` — the `.pushed(group_tag)` at `fixpp_msg_get_group` is well-formed.
- **`len+1` slice-widening safety** (`build_nested_subview` RC1, `:640-647`) already holds for C-ABI slices — they come from the same `group_slices()` the typed path uses; interior-SOH is guaranteed by the well-formed frame.
- **Zero-global-heap preserved** (FR-007/SC-004): the nested sub-table + its slices are allocated from `resource()` (the per-message arena), exactly as `build_nested_subview` already does for the typed path, and the cursor shell continues to allocate from `resource()`.
- **Depth/entry caps — over-limit caught at the OUTER read, unreachable on the nested path** (FR-009): `consume_group_extent` enforces `kMaxGroupDepth=16` and `cfg_.max_group_entries_per_instance`, but the span-returning `nested_group_slices` → `group_slices()` **collapses** an `err_group_too_large` to an empty span (`offset_table.cpp:583/621/625`) — so the C-ABI does **NOT** "inherit `err_group_too_large` by reusing `consume_group_extent`" through the span API; that earlier claim is false. What actually makes this safe is **unreachability**: (a) the only way to obtain a nested cursor is `fixpp_msg_get_group`, whose mandatory outer-first `group_slices()` pre-walk already recurses through the nested group during the outer extent computation (`consume_group_extent` recurses at `offset_table.cpp:476-482`), and (b) the C-ABI always parses with the default `Config` (no tuning surface for `max_group_entries_per_instance`, `config.cpp`; inbound parse builds `Parser<Index>` with default `Config`), so the sub-table's caps equal the root's. An over-limit nested group therefore fails the OUTER read first (`fixpp_msg_get_group` → `FIXPP_ERR_TYPE_MISMATCH`) and the consumer never descends. The old scanner's coded `WIRE_LIMIT_EXCEEDED`/`kMaxNested=256` guard (untested / `LCOV_EXCL`) is **dropped** — acceptable only because the nested overflow cannot independently arise, not because it regressed a tested behavior. (A status-bearing seam — e.g. a private helper returning the `group()` error — is optional belt-and-suspenders hardening, **not** required for correctness here; do not widen the span primitive.)
