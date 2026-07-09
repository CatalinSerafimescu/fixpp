# Implementation Plan: Membership-aware C-ABI nested repeating-group read

**Branch**: `065-cabi-nested-group-membership` | **Date**: 2026-07-08 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/065-cabi-nested-group-membership/spec.md` (fixes issue #179 / L-063-2)

> **Re-plan provenance (2026-07-09).** Artifacts imported from the parked `065-…-preplan` branch and re-based on merged **066** (PR #181, squash `4e3be8e5`). 065 was parked when its Gate A surfaced the *dict-free-parse premise error*: the shipped C-ABI inbound path built its `Parser` dictionary-free (`Session::parse_and_dispatch_`, `session.cpp:316`), so `OffsetTable::stored_group_context()` returned **empty** on a dispatched C-ABI view and the nested read had no membership to bound with. 066 fixed exactly that — inbound now parses with `Parser{*inbound_tv_}` from `cfg_.dictionary->as_table_view()`; the post-066 C-ABI loopback test `GroupMembershipCapiRed.TrailingFieldAbsentFromLastInstance` confirms membership reaches the C-ABI view. **The premise is now satisfied.** Line/anchor references below predate 066's `offset_table.cpp` / `message_read.cpp` restructure (±10–20 lines — e.g. `struct fixpp_group` is now `capi_internal.hpp:310`, not `:291`; the target defect function `fixpp_group_get_nested_group` is untouched by 066, still `message_read.cpp:462-615`) and are re-validated + re-anchored fresh at `/speckit-clarify` → Gate A against 066-as-merged.

## Summary

`fixpp_group_get_nested_group` slices a nested repeating group with a hand-rolled, dictionary-membership-free positional scanner and closes the **last** nested instance at the end of the outer entry's slice (`src/capi/message_read.cpp:475-599`, `:591-596`). When a declared outer-group member follows a multi-entry nested group (ubiquitous: 222 layouts in FIX44, ~20k in FIX50SP2), that trailing member's bytes fall inside the last nested instance, so `fixpp_group_get_field_*(nested, last, trailing_tag, …)` returns `FIXPP_ERR_OK` + a wrong value where a correct reader must return `FIXPP_ERR_TAG_NOT_FOUND` — a reachable, any-counterparty **silent wrong value** on the GA-frozen C-ABI (L-063-2).

**Approach**: delete the hand-rolled scanner and delegate nested-instance slicing to the existing membership-aware primitive `OffsetTable::nested_group_slices` (062), which bounds every instance via the same context-threaded `consume_group_extent` walk (063) that makes the C++ typed path correct — so the two paths agree by *sharing the primitive*, not by re-deriving membership (FR-005). The only new input the C-ABI cursor lacks is the nested-descent **context**; carry it as one `group_context` field on the **internal** `fixpp_group` cursor (zero public-ABI) plus two non-ABI `OffsetTable` seams (public C++): a `nested_group_slices` convenience overload and an out-of-line `group_context_for` seed accessor. This is the membership-aware C-ABI follow-up feature that 063 `plan.md:106` explicitly deferred; the change is a net deletion + primitive reuse, answering 063 Round-2's "gratuitous rewrite" objection (research Decision 5).

## Technical Context

**Language/Version**: C++23. **Primary surface**:
- `src/capi/message_read.cpp` — `fixpp_group_get_nested_group` (`:462-615`): replace Phase-1/Phase-2 hand-rolled scanner with a `nested_group_slices` call + absent-vs-empty presence probe (research Decision 1/6); `fixpp_msg_get_group` (`:336-380`): seed `grp->group_ctx = view->offsets().group_context_for(group_tag)` — a new public C++ (non-C-ABI) accessor `OffsetTable::group_context_for(std::uint16_t no_tag) const noexcept` returning `stored_group_context().pushed(no_tag)` (the raw `stored_group_context()` is `private`, so a non-member C-ABI free function cannot reach it — research Decision 2).
- `src/capi/capi_internal.hpp` — `struct fixpp_group` (`:291`): add one `fixpp::wire::group_context group_ctx{}` field (research Decision 2).
- `include/fixpp/wire/offset_table.hpp` — one convenience overload of `nested_group_slices` that forwards to the 7-arg overload using the table's own `opaque_dict_`/`group_member_fn_` and a build-mode-safe token via a private `token_for_nested_cache() const noexcept` helper (`#ifndef NDEBUG return gen_; #else return {};`) — `gen_` is `NDEBUG`-guarded (`offset_table.hpp:262-264`), so forwarding it directly would not compile in release; the helper makes the overload compile in both modes (research Decision 4). Public **C++**, non-ABI.

**Untouched by contract**: `OffsetTable::nested_group_slices` (7-arg) / `build_nested_subview` / `consume_group_extent` *algorithms* and cache keying (FR-005); the C++ typed read path; the loader / dictionary / `table_view`; wire framing; the writer/emitter (no codegen change — this touches no generated accessor); all other C-ABI functions.

**C-ABI freeze**: no exported C symbol, public C header, `fixpp_error_t` value, or version constant changes. `tests/abi/golden/fixpp_capi_symbols.txt` + `tools/capi_freeze.sha256` unchanged (SC-003). The `fixpp_group` field is on the internal struct, opaque to consumers.

**Testing**: GoogleTest under `tests/capi/` (+ existing `tests/wire/`, `tests/codegen/`, `tests/alloc_guard/` gates run unchanged). **Target Platform**: Linux Tier-1 + Windows Tier-2 + libc++ Tier-3. **Project Type**: single library. **Scale/Scope**: one C-ABI read function reworked to reuse an existing primitive; no new message families, no builders, no dictionary data, no codegen/goldens.

**Performance/Constraints**: nested read stays allocation-free of the global heap (sub-table + slices from the per-message arena, as `build_nested_subview` already does — FR-007/SC-004); extent walk is the depth-capped (`kMaxGroupDepth=16`) stack-only `consume_group_extent` (FR-009).

## Constitution Check

*GATE: must pass before Phase 0 (passed — research complete) and re-checked post-design.*

- **Appendix A mandatory triggers**: **C-ABI surface** (behavioral correction of a C-ABI read; no symbol change) AND **Wire/parser-adjacent** (nested-group extent semantics) → run the mandatory controls: `/speckit-clarify` (done — dual-witness clarification 2026-07-09; degradation fork resolves by-construction, research Decision 3), Codex **Gate A** (**converged** round 2, 1 rewrite, user-signed-off 2026-07-10 — see `## Gate A` below + `.specify/decisions/065-…-gatea.md`), user `/plan` sign-off (**done** 2026-07-10), `/speckit-analyze` (**done** 2026-07-10 — 0 CRITICAL/HIGH; E-1 FR-008-coverage gap resolved by tasks.md T011). Full **Gate B** before merge.
- **Error semantics**: no new `fixpp_error_t` value; reuses `FIXPP_ERR_OK` / `FIXPP_ERR_TAG_NOT_FOUND` / existing group-limit errors. The fix changes *which extent* bounds the last instance, and preserves the absent-vs-empty-count mapping exactly (research Decision 6; pinned by `NestedGroupAbsentTag`, `NestedGroupEmptyGroupCountLastField`).
- **C-ABI** (`[const §X.1]`, GA-frozen 1.5.0): **byte-identical exported surface** — outputs of `fixpp_group_get_nested_group`/`fixpp_group_get_field_*` become correct for the trailing-member layout; no exported symbol / header / enum / version change (SC-003).
- **Zero-alloc discipline** (`[const §VIII.5]`): nested read allocates only from the per-message arena; global-heap-free (FR-007; alloc-gate-covered). The deleted scanner used a 256-slot stack array; the primitive uses the arena exactly as the typed path — no new global heap.
- **TDD / discriminating witnesses** (`[const Art VII §3]` TDD mandate; coverage/sanitizer gates `[const Art IX]`): the fix lands red→green on the pre-authored `GTEST_SKIP`'d witness `NestedGroupLastInstanceExtentDoesNotAbsorbTrailingOuterMember` (un-skip = FR-010/SC-001), which must be **mutation-proven RED** on the pre-fix code, and green after. All existing `tests/capi/` nested tests stay green (SC-002).
- **Sanitizers as real defects**: nested sub-table lifetime + slice extents validated under the full ASan/UBSan/TSan matrix, not asserted.
- **Cross-platform**: the C-ABI read is platform-agnostic; existing Tier-2/Tier-3 lanes cover it; no new platform-only branch.

**Post-design re-check** (after Phase 1): the **three** surface additions, all **non-C-ABI** — the internal `fixpp_group::group_ctx` field, the `OffsetTable::nested_group_slices` convenience overload, and the public `OffsetTable::group_context_for` seed accessor (out-of-line in `offset_table.cpp`) — the cursor struct is internal (public header only forward-declares it), and the two `OffsetTable` seams are public C++ recompiled with the library. The C-ABI exported surface is byte-identical. Recorded in Complexity Tracking. No unresolved clarifications.

## Project Structure

### Documentation (this feature)

```text
specs/065-cabi-nested-group-membership/
├── plan.md              # This file
├── research.md          # Phase 0 — 6 mechanism decisions
├── data-model.md        # Phase 1 — cursor context + extent entities
├── quickstart.md        # Phase 1 — how to reproduce/verify (the witness)
├── contracts/
│   └── cabi-nested-read.md   # Phase 1 — observable C-ABI nested-read contract
├── checklists/
│   └── requirements.md  # spec quality (closed at specify/clarify)
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
src/capi/capi_internal.hpp    # struct fixpp_group (:291): + fixpp::wire::group_context group_ctx{}
                              #   (internal struct; public header only forward-declares → zero C-ABI change).
                              #   Ensure group_view.hpp (group_context definition) is included (via parser.hpp).
src/capi/message_read.cpp     # fixpp_msg_get_group (:336): seed grp->group_ctx = offsets().group_context_for(group_tag)
                              # fixpp_group_get_nested_group (:462-615): DELETE Phase-1/Phase-2 positional scanner
                              #   (:475-599 incl. LCOV_EXCL :554-562 dead block); CALL
                              #   parent_view->offsets().nested_group_slices(sl->data, sl->len, nested_tag, parent->group_ctx);
                              #   map empty span via presence-probe (absent→TAG_NOT_FOUND, present→OK/nc=0);
                              #   set nested->group_ctx = parent->group_ctx.pushed(nested_tag)
include/fixpp/wire/offset_table.hpp  # + nested_group_slices(slice_data, slice_len, nested_no_tag, ctx) const overload
                              #   forwarding to the 7-arg overload with opaque_dict_/group_member_fn_ + a build-mode-safe token via
                              #   private token_for_nested_cache() const noexcept (#ifndef NDEBUG return gen_; #else return {};) —
                              #   gen_ is NDEBUG-only (:262-264), so a direct gen_ forward won't compile in release (public C++, non-ABI)
                              # + group_context_for(std::uint16_t no_tag) const noexcept — DECLARED here, DEFINED OUT-OF-LINE in
                              #   src/wire/offset_table.cpp (returns stored_group_context().pushed(no_tag)). MUST be out-of-line:
                              #   group_context is only FORWARD-DECLARED in this header (constituent fields stored to avoid pulling in
                              #   group_view.hpp), so an inline .pushed() body needs the complete type and would fail to compile in every
                              #   TU including the header — the identical trap that forced last round's RC#3 token helper out-of-line;
                              #   mirrors how stored_group_context() itself is header-declared / cpp-defined (public C++, non-ABI)
src/wire/offset_table.cpp     # + OUT-OF-LINE definition of OffsetTable::group_context_for (the only touched line here); the
                              #   nested_group_slices(7-arg)/build_nested_subview/consume_group_extent ALGORITHMS remain UNTOUCHED (FR-005)

# UNTOUCHED (FR-005): nested_group_slices(7-arg)/build_nested_subview/consume_group_extent algorithms + cache keying;
#   C++ typed read path; loader/dictionary/table_view; wire framing; codegen emitter + goldens; every other C-ABI fn.

tests/capi/CMakeLists.txt    # + witness (b)'s target: a NEW dict066-style engine-loopback target (mirroring capi_dict066_group_membership_red_test,
                              #   :418-432) OR a new case on the existing dict066 loopback target — a target that ALREADY carries FIXPP_DICT_DATA_DIR +
                              #   FIXPP_CAPI_FEATURE_B_INCLUDES + the capi_dict066_loopback_support.hpp scaffold. capi_message_read_test is UNCHANGED
                              #   (no compile-def added): witness (a) is inline-XML and needs no FIXPP_DICT_DATA_DIR.
tests/capi/message_read_test.cpp   # UN-SKIP NestedGroupLastInstanceExtentDoesNotAbsorbTrailingOuterMember (:1101) — FR-010/SC-001;
                              #   remove its GTEST_SKIP + escalation comment; keep all its positive assertions; mutation-prove RED on pre-fix code.
                              #   ADD (FR-011/SC-005) — witness (a) here; witness (b) lives in the dict066-loopback target above [clarify 2026-07-09]:
                              #     (a) DIRECT as_table_view() witness — inline-XML XmlLoader{}.load_from_string(<FIX44-shaped XML>, &arena) then dict.as_table_view()
                              #         (per the EXISTING precedent TopLevelCollidingGroup296CAbiReadsFullMassQuoteExtent, :1778-1791) on the
                              #         NoLegs(555)→NoLegSecurityAltID(604)→trailing LegQty(687) shape — trailing member TAG_NOT_FOUND on last nested instance
                              #         + C-ABI==C++ typed read equivalence (isolates extent arithmetic). NO FIXPP_DICT_DATA_DIR, NO CMakeLists change,
                              #         mallocnesia-safe inside capi_message_read_test (the 296 precedent already runs there, incl. under the mallocnesia gate).
                              #         as_table_view() still yields a genuine context-scoped registration (NOT a hand-built single-msg_type table_view — spec.md:69).
                              #     (b) ENGINE-LOOPBACK witness (GroupMembershipCapiRed-style, via tests/capi/capi_dict066_loopback_support.hpp) driving
                              #         the same frame through the 066 C-ABI dispatch path — trailing member TAG_NOT_FOUND on last nested instance via the
                              #         PRODUCTION context path (pins real msg_type/parent-path threading — the 063 Gate-B RC#1 empty-msg_type class).
                              #         CANNOT live here (message_read_test.cpp is a synchronous InboundHandle unit target with no loopback scaffold and no
                              #         FIXPP_DICT_DATA_DIR) — it belongs in the dict066-loopback target above; it may use the shipped FIX44.xml (that harness
                              #         already carries FIXPP_DICT_DATA_DIR).
                              #   NO membership-collision test — C5 is a SCOPE LIMITATION (value-based predicate cannot exclude a same-valued
                              #     trailing tag), tracked as L-062-3 / L-063-4 / issue #180; the disjoint (distinct-tag) behavior is already
                              #     covered by the primary witness (tag 999) + the FR-011 witness (687).  DEPTH-1 ONLY (research Decision 7).
                              #   RE-RUN existing NestedGroup* (absent / empty-count-last / per-entry-distinct / multi-instance) unchanged (SC-002).
                              #   BEFORE un-skipping: grep the existing NestedGroup* suite for any case pinning the OLD positional nc on a
                              #     non-terminal zero/short nested count where the more-correct declared-count/membership path could flip nc;
                              #     confirm none regress (SC-002; research New #3).
tests/abi/                    # capi_freeze.sha256 + fixpp_capi_symbols.txt UNCHANGED (SC-003) — verified, not edited.
```

**Structure Decision**: single-library layout; the change is confined to the C-ABI read layer (`src/capi/`) plus two non-ABI `OffsetTable` seams — the `nested_group_slices` convenience overload (`include/fixpp/wire/offset_table.hpp`) and the out-of-line `group_context_for` seed accessor (`include/fixpp/wire/offset_table.hpp` declaration + `src/wire/offset_table.cpp` definition). No new modules, no codegen/goldens, no C-ABI symbol change.

## Complexity Tracking

> Three surface additions, all **non-C-ABI**, all required to reuse the shared membership primitive (the alternative — a bespoke C-ABI membership scan — is the greater complexity and is FR-005-forbidden).

| Addition | Why Needed | Simpler Alternative Rejected Because |
|---|---|---|
| `struct fixpp_group` (internal, `capi_internal.hpp:291`) gains one `group_context group_ctx{}` field | `nested_group_slices` needs the nested-descent context (`{msg_type, parent-path}`); the C-ABI cursor is the only place that knows which group/path led here. Symmetric with the typed path's `entry_context::group_ctx`. | Reconstructing context inside `OffsetTable` is impossible (the root table doesn't know the caller's descent path). Public ABI is untouched — the struct is internal (public header forward-declares only). Only `group_ctx` is added — not the `no_tag`-too shape 063 Round-2 rejected. |
| `OffsetTable::nested_group_slices(slice_data, slice_len, nested_no_tag, ctx)` overload + private `token_for_nested_cache()` helper (public C++, non-ABI) | The root `OffsetTable` already stores `opaque_dict_`/`group_member_fn_` privately (and `gen_` in debug only); the C-ABI caller has the table but not those members. One encapsulated overload beats exposing private-member accessors. The token helper (`#ifndef NDEBUG return gen_; #else return {};`) is required because `gen_` is `NDEBUG`-only (`offset_table.hpp:262-264`) — forwarding `gen_` directly would fail to compile in release. | Public accessors leak internal wiring; a `gen_`-forwarding inline body would not build in release; same non-ABI category as 063's `group_member_fn_t` context-param widening. |
| `OffsetTable::group_context_for(std::uint16_t no_tag) const noexcept` seed accessor — DECLARED in `offset_table.hpp`, DEFINED OUT-OF-LINE in `src/wire/offset_table.cpp` (public C++, non-ABI) | `fixpp_msg_get_group` seeds the top-level cursor's `group_ctx` from `stored_group_context().pushed(group_tag)`, but `stored_group_context()` is `private` (`offset_table.hpp:257`) and the seed executes in a non-member C-ABI free function that cannot reach it. One public accessor encapsulates the `.pushed()` protocol. It MUST be out-of-line: `group_context` is only forward-declared in the header, so an inline `.pushed()` body needs the complete type and won't compile in TUs including the header — mirrors `stored_group_context()` itself (header-declared / cpp-defined). | Publishing `stored_group_context()` raw leaks the root context + forces the caller to know `.pushed()`; a `friend` on the C-ABI TU couples the unit to `OffsetTable` internals; an inline body won't compile (incomplete `group_context`). |

## Phase notes
- **Phase 0** (done): research.md — 7 decisions (reuse primitive; internal-cursor context; by-construction degradation; convenience overload; 063-reversal sanction; absent-vs-empty probe; **depth-1 scope + surfaced pre-existing typed depth-2 gap**).
- **Phase 1** (done): data-model.md (cursor context propagation + nested-extent entities), contracts/cabi-nested-read.md (observable C-ABI contract, before/after, depth-1 scope), quickstart.md (the witness + full C-ABI suite + ABI freeze verify). Agent-context marker refresh.
- **Phase 2** (`/speckit-tasks`, later): tasks.md — overload; cursor field + include; seed at get_group; rework nested read + presence probe; child-context propagation (`.pushed`); un-skip witness (mutation-proven RED first); ADD two FR-011 witnesses [clarify 2026-07-09] — (a) direct `as_table_view()` witness as an inline-XML `load_from_string`→`as_table_view()` test in `message_read_test.cpp` (296 precedent, no `FIXPP_DICT_DATA_DIR`), asserting FIX44-legs C-ABI==typed equivalence (via member values/extent + `field_value()` absence — NOT a typed accessor for the non-member trailing tag) and (b) an engine-loopback dispatch-path witness in a dict066-style loopback target (add `tests/capi/CMakeLists.txt`) via `capi_dict066_loopback_support.hpp`; grep existing NestedGroup* for old-positional-nc pins on a non-terminal zero/short count before un-skipping (SC-002); re-run existing nested tests; ABI freeze verify; alloc/sanitizer gates; B&L L-063-2 retire + record candidate L-065-1 (depth-2 C-ABI-vs-typed divergence / typed gap follow-up) + close-out. NO membership-collision test (C5 is a scope limitation — L-062-3 / L-063-4 / #180).

**Scope boundary**: this feature is **depth-1** (issue #179). Research Decision 7 surfaced a *pre-existing* typed-path context-threading gap at depth ≥ 2 (generated nested accessor threads the parent's unpushed context) — recorded as candidate `L-065-1` + a follow-up, **not fixed here** (would require emitter change + forced golden regen). SC-005 equivalence is scoped to the depth-1 layout.

## Gate A

- Round 1 (PREPLAN branch `065-…-preplan`) applied 2026-07-08: Codex P1=2 P2=2 P3=0; Opus post-judging P1=1 P2=4 P3=3; rewrite addresses root causes: (RC#2) C5 membership-collision recast to a disjoint-dict scope limitation citing L-062-3 / L-063-4 / issue #180 (spec Edge Cases, contract C5/C5a, collision test removed from plan); (RC#1) false "inherits `err_group_too_large` fail-closed" claim corrected to the verified unreachability argument (outer-first pre-walk + always-default C-ABI `Config`), coded `WIRE_LIMIT_EXCEEDED` guard acknowledged as dropped (research :110, FR-009, contract C6, Decision 6 cross-link); (RC#3) 4-arg overload made release-compilable via a private `token_for_nested_cache()` helper (`gen_` is `NDEBUG`-only) applied uniformly across research Decision 4, data-model, plan Technical Context / Structure / Complexity Tracking; (RC#4) quickstart §6 made mandatory + concrete with the FR-011 witness shape and the typed-path writability caveat (equivalence via member values/extent + `field_value()` absence); plus depth-2 C-ABI-vs-typed divergence recorded as candidate L-065-1 (Decision 7), an SC-002 pre-un-skip grep note, and the `[const Art VII §3]` TDD citation fix. Reviews: research/reviews/codex_065-cabi-nested-group-membership_gate_a_review.md, research/reviews/opus_065-cabi-nested-group-membership_gate_a_adversarial_review.md.
- Round 1 (post-066 re-plan) applied 2026-07-09: Codex P1=1 P2=2 P3=1; Opus post-judging P1=1 P2=3 P3=2; rewrite addresses RC#1 (private stored_group_context seed → public out-of-line group_context_for), RC#2 (two FR-011 witnesses' build homes: inline-XML (a) in message_read_test.cpp + dict066-loopback (b); add tests/capi/CMakeLists.txt), RC#3 (contract C7 two-witness + quickstart Parser{*inbound_tv_} mechanism). Reviews: research/reviews/codex_065-cabi-nested-group-membership_gate_a_review.md, research/reviews/opus_065-cabi-nested-group-membership_gate_a_adversarial_review.md.

### Round 1 — disagreements

- **Opus DOWNGRADED Codex's error-erasure finding P1→P2 and REJECTED the `expected<span,error>` counter-proposal.** Codex proposed adding a status-bearing primitive (`nested_group_slices_result` / `expected<span, error>`) to map `err_group_too_large` to a C error. Opus verified the over-limit condition is **unreachable** on the public C-ABI nested read (the mandatory outer-first `group_slices()` pre-walk recurses through the nested group, and the C-ABI always parses with the default `Config`, so sub-table caps equal the root's — the outer read fails first). A corrected-unreachability doc therefore suffices; widening the primitive is over-engineering. **Not adopted verbatim** — the rewrite states unreachability + acknowledges the dropped (untested/`LCOV_EXCL`) coded guard instead of adding an API.
- **Opus CORRECTED Codex's overstated delimiter-collision sub-case.** Codex implied a trailing tag equal in value to the nested group's **delimiter** would also mis-slice. Opus verified the outer instance loop is bounded by `inst < declared` (`offset_table.cpp:464`), so on a **well-formed** count the walk stops after `declared` instances and a trailing delimiter-valued tag is left outside; delimiter-collision `nc` inflation arises only under a **lying** declared count (a separate, already-fail-closed concern). The collision finding is therefore scoped to the **non-delimiter member-value** collision (C5), not the delimiter case. **Not carried into the rewrite** as framed by Codex.
