# Implementation Plan: Dictionary-backed inbound receive parse

**Branch**: `066-dict-backed-inbound-parse` | **Date**: 2026-07-09 | **Spec**: [spec.md](./spec.md)
**Input**: Prerequisite for issue #179 / feature 065 (Fable investigation 2026-07-09; user decision Option A).

## Summary

`Session::parse_and_dispatch_` (`src/session/session.cpp:316`) parses every inbound frame with a **default (dictionary-free) `Parser<access_mode::Index>`**, and every application callback (C-ABI and C++) is handed the resulting `MessageView`. So on the shipped path the root `OffsetTable` has null `opaque_dict_`/`group_member_fn_`, `OffsetTable::group()` takes its membership-free fallback (`group_end = entries_.size()`), and every inbound repeating-group read (C-ABI *and* C++ typed) is positional — the last instance runs to end-of-message and absorbs trailing fields (the root cause behind #179; also affects top-level groups and the scalar-as-group `TYPE_MISMATCH` guard). The dictionary is already **required** at `open()` but currently feeds only the opt-in validator and outbound-message creation.

**Approach**: build the configured dictionary's `table_view` **once** at `open()` as a stable-address `Session` member, and construct the inbound `Parser` **dictionary-backed** from it, so inbound `MessageView`s carry membership. Both read paths then become membership-correct by construction (they derive membership from the same `OffsetTable`). Propagate the same membership into `fixpp_msg_clone` and `reify` views so a clone/reified handle reads identically to its dict-backed source (FR-007, user-clarified). The unknown-field-in-group behavior becomes strict (matches QuickFIX/J) — accepted (FR-008), documented, with the extension story = dictionary currency + dialect overlays. This uses the already-supported dictionary-backed `Parser` ctor (`parser.hpp:502-540`) + the 062/063 membership/extent machinery unchanged; it does not modify the parse/membership algorithms.

## Technical Context

**Language/Version**: C++23. **Primary surface**:
- `include/fixpp/session/session.hpp` — add a stable-address `table_view` member (e.g. `std::optional<fixpp::dict::table_view> inbound_tv_;`) analogous to how `validator_` already holds one; declared so its address is stable for the session lifetime (the dict-backed `Parser` ctor stores `std::addressof(dict_metadata)`).
- `src/session/session.cpp` — `open()` (~:1160): build `inbound_tv_ = cfg_.dictionary->as_table_view()` once (dictionary is required by the null-dict guard ~:929). `parse_and_dispatch_` (~:316): construct `Parser<access_mode::Index>{*inbound_tv_}` instead of the default. Assess `vg_parser` (~:1869) — the opt-in validator's own parse (FR-006).
- `src/capi/engine.cpp` (~:77-90) — the inbound `fixpp_msg` callback-wrap: carry the session's dictionary (`shared_ptr<const Dictionary>`, already cached at `src/capi/session.cpp:111`) onto the inbound handle so `fixpp_msg_clone` can propagate membership (today inbound `dict_ == nullptr`).
- `src/capi/message_write.cpp` (~:428-441) — `fixpp_msg_clone`: copy `dict_`; build a **clone-owned** `table_view` (stable address, owned like `owned_view_`); construct the clone's `MessageView` dictionary-backed so a clone reads groups identically to its source (FR-007).
- `src/dictionary/reify.cpp` (~:99-121) — `owning_message_handle::view()` re-frames dict-free; carry the dictionary into the owning handle so the re-framed view is dictionary-backed (FR-007).

**Untouched by contract**: the `Parser` dict-backed ctor, `OffsetTable`/`consume_group_extent`/`nested_group_slices`, `as_table_view()`, the generated flyweights — all *algorithms* unchanged (this feature routes the existing dict-backed parse into the session/clone/reify paths). No wire-framing, no error-enum, no C-ABI **symbol** change (behavioral only). No new message families/builders/dictionary data.

**C-ABI**: no exported C symbol / public header / error value / version change (GA-frozen 1.5.0). The change is behavioral (inbound group reads become membership-correct) — surfaced as a Behaviors & Limitations row (FR-008), not an ABI event.

**Testing**: GoogleTest under `tests/session/`, `tests/capi/`, `tests/interop/`. **The load-bearing shift**: grouped-read witnesses MUST drive a frame through **real `Session` dispatch** (and a C-ABI engine loopback), RED-first on the pre-change dict-free parse — not `Parser<Index>{dict}` unit parses (which already pass and masked this). Existing dict-backed unit tests remain the membership-logic tier. **Target Platform**: Linux Tier-1 + Windows Tier-2 + libc++ Tier-3.

**Performance/Constraints**: `as_table_view()` built once per `open()` (not the hot path; the validator already pays it when enabled — consider sharing the two builds). Per-frame cost: root `group_context` seed (one `msg_type()` find) + membership lookups **only on an actual group read** (lazy) — admin/no-group traffic ≈ unchanged. No new **global-heap** allocation on the parse+read path: per-message membership + lazily-built nested sub-views come from the existing per-message stack arena (`kInboundParseArena=16384`, `kAdminParseArena=8192`); confirm representative group-bearing messages fit, and that the depth/entry caps still fail closed (FR-009).

## Constitution Check

*GATE: must pass before Phase 0 (passed — research complete) and re-checked post-design.*

- **Appendix A mandatory triggers**: **Session inbound path** + **Wire/parser** (parse behavior change) → run mandatory controls: `/speckit-clarify` (done — 2 decisions recorded: strict membership accepted; clone/reify propagated), Codex **Gate A** (pending), `/speckit-analyze` (pending, after `/tasks`), user `/plan` sign-off (pending). Full **Gate B** before merge.
- **Error semantics**: no new `fixpp_error_t`; restores the documented `TYPE_MISMATCH` for scalar-as-group and membership-bounded extents. Behavior change (strict in-group) documented (FR-008).
- **C-ABI** (`[const §X.1]`, GA-frozen 1.5.0): no exported symbol/header/enum/version change; behavioral correction only (`tests/abi` golden unchanged).
- **Zero-alloc discipline** (`[const §VIII.5]`): the `table_view` build is at `open()` (not the hot path); the per-message parse+read path adds no new global-heap allocation (membership lazy; sub-views from the per-message arena) — alloc-gate-verified.
- **TDD / discriminating witnesses** (`[const Art VII §3]` TDD; `[const Art IX]` coverage/sanitizers): each behavior change lands RED→GREEN, RED-first proven **through real Session dispatch** (the dict-backed unit tests cannot prove the shipped path). Existing session/interop/C-ABI suites stay green; each intended delta is an explicit, reviewed, discriminating test edit (no silent breakage).
- **Sanitizers as real defects**: the `table_view` member lifetime (stable address bound by the Parser) + clone-owned `table_view` + reify view lifetime validated under ASan/UBSan/TSan (the dict-backed parse touches per-message arena lifetimes on the session path for the first time).
- **Cross-platform**: session parse is platform-agnostic; existing Tier-2/Tier-3 lanes cover it.

**Post-design re-check** (after Phase 1): the surface additions — a `Session` `table_view` member, a clone-owned `table_view`, a dictionary handle on the reify owning handle, and threading the dict onto the inbound `fixpp_msg` — are all internal (no C-ABI symbol/header). The behavioral change (strict in-group membership) is the intended fix + a documented B&L row. No unresolved clarifications.

## Project Structure

### Documentation (this feature)

```text
specs/066-dict-backed-inbound-parse/
├── plan.md, research.md, data-model.md, quickstart.md
├── contracts/inbound-parse.md
└── checklists/requirements.md
```

### Source Code (repository root)

```text
include/fixpp/session/session.hpp   # + stable-address inbound table_view member (mirror validator_'s ownership)
src/session/session.cpp             # open(): build inbound_tv_ once; parse_and_dispatch_(:316): Parser{*inbound_tv_};
                                    #   assess vg_parser(:1869) (FR-006)
src/capi/engine.cpp                 # inbound fixpp_msg callback-wrap: carry session dict (shared_ptr) for clone (FR-007)
src/capi/message_write.cpp          # fixpp_msg_clone(:428): copy dict_; clone-owned table_view; dict-backed clone MessageView (FR-007)
src/capi/capi_internal.hpp          # fixpp_msg: clone-owned table_view member (owned like owned_view_) (FR-007)
src/dictionary/reify.cpp            # owning_message_handle: dict-backed re-frame (carry dictionary) (FR-007)
spec/behaviors-and-limitations.md   # NEW B-066-*/L-066-* row: permissive->strict in-group (QuickFIX-aligned) + extension story;
                                    #   AMEND L-063-2 ("C++ typed path unaffected" false on shipped path) (FR-010)

# Tests
tests/session/                      # US1/US2 through REAL Session dispatch: group-bearing frame -> callback asserts
                                    #   membership-bounded extent (trailing field TAG_NOT_FOUND on last instance) + scalar-as-group TYPE_MISMATCH.
                                    #   RED-first on dict-free parse. (SC-001/002)
tests/capi/                         # C-ABI engine loopback: same via fixpp_group_get_field_* in a registered receive callback;
                                    #   clone reads identically to source (FR-007). No abi golden change (SC-003).
tests/interop/ + tests/session/     # regression sweep: existing suites green; each intended delta an explicit reviewed edit (SC-003)
tests/alloc_guard/                  # no new global-heap on parse+read path (SC-004)
```

**Structure Decision**: single-library; changes span session (the parse-site + member), the C-ABI clone + inbound-handle dict plumbing, and reify — all consuming the existing dict-backed parse. No new modules; no C-ABI symbol change; no codegen/goldens.

## Complexity Tracking

> The core (session parse dict-backed) is small. FR-007 (clone/reify propagation) is the added surface — justified by the user's clarify decision (avoid the silent clone-reads-differently footgun) and bounded below.

| Addition | Why Needed | Simpler Alternative Rejected Because |
|---|---|---|
| `Session` stable-address `table_view` member built at `open()` | The dict-backed `Parser` ctor stores a pointer to the `table_view`; it must outlive every parse. Mirrors the validator's existing owned `table_view`. | A per-message `as_table_view()` rebuild — rejected (hot-path allocation, FR-004). |
| Inbound `fixpp_msg` carries the session dict + `fixpp_msg_clone` owns a `table_view`, dict-backed clone view | FR-007: a clone outlives the dispatch window and cannot borrow the session's `table_view`; to read identically it needs an owned membership source. Inbound `dict_` is null today. | Leaving clones dict-free — rejected by the user (silent divergence footgun). |
| Reify owning handle carries the dictionary; dict-backed re-frame | FR-007: reify copies bytes and re-frames dict-free, losing the source's membership. | Documenting reify divergence — rejected by the user (propagate). |

## Phase notes
- **Phase 0** (done): research.md — the dict-free-parse root cause + evidence; the reuse-the-existing-dict-backed-Parser mechanism; the strict-membership/Orchestra reconciliation; clone/reify lifetime; validator-parse assessment; regression-risk + real-dispatch test strategy.
- **Phase 1** (done): data-model.md (the session table_view member + membership propagation entities), contracts/inbound-parse.md (observable before/after), quickstart.md (RED-first real-dispatch witness). Agent-context refresh.
- **Phase 2** (`/speckit-tasks`, later): tasks.md — session member + open() build; parse_and_dispatch_ dict-backed; vg_parser assessment; real-dispatch US1/US2 witnesses (RED-first); regression sweep; clone dict propagation + clone-identity witness; reify dict propagation + witness; alloc/sanitizer gates; B-066/L-066 + L-063-2 amendment; #179 amendment (comment posted 2026-07-09); close-out. Note the 065 dependency (065 re-plans + verifies through real dispatch on top of this).

## Gate A

*Pending — populated by `/gate-a 066-dict-backed-inbound-parse` before `/speckit-tasks`.*
