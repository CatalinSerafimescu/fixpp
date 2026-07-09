# Implementation Plan: Dictionary-backed inbound receive parse

**Branch**: `066-dict-backed-inbound-parse` | **Date**: 2026-07-09 | **Spec**: [spec.md](./spec.md)
**Input**: Prerequisite for issue #179 / feature 065 (Fable investigation 2026-07-09; user decision Option A).

## Summary

`Session::parse_and_dispatch_` (`src/session/session.cpp:316`) parses every inbound frame with a **default (dictionary-free) `Parser<access_mode::Index>`**, and every application callback (C-ABI and C++) is handed the resulting `MessageView`. So on the shipped path the root `OffsetTable` has null `opaque_dict_`/`group_member_fn_`, `OffsetTable::group()` takes its membership-free fallback (`group_end = entries_.size()`), and every inbound repeating-group read (C-ABI *and* C++ typed) is positional — the last instance runs to end-of-message and absorbs trailing fields (the root cause behind #179; also affects top-level groups and the scalar-as-group `TYPE_MISMATCH` guard). The dictionary is already **required** at `open()` but currently feeds only the opt-in validator and outbound-message creation.

**Approach**: build the configured dictionary's `table_view` **once** at `open()` as a stable-address `Session` member, and construct the inbound `Parser` **dictionary-backed** from it, so inbound `MessageView`s carry membership. Both read paths then become membership-correct by construction (they derive membership from the same `OffsetTable`) **for dictionaries whose `table_view` actually registers groups (e.g. FIX44 / FIX50 / FIXT)**. Per inherited **L-063-1** (`spec/behaviors-and-limitations.md:1695`) FIX 4.0/4.1/4.2 type their group-count fields as legacy XML `INT` (not `NUMINGROUP`), so `as_table_view()` registers ZERO groups for them; dict-backing a FIX4x session flips its group reads to `TYPE_MISMATCH`/absent (strict-but-group-blind). Structural INT-count group registration is out of scope for 066 (feature-sized, L-063-1 defers it) — 066 scopes its correctness claims to group-registering dicts and adds an explicit FIX4x limitation row (see spec FR-001/003, SC-001, FR-008/FR-010). Propagate the same membership into `fixpp_msg_clone` and `reify` views so a clone/reified handle reads identically to its dict-backed source (FR-007, user-clarified). The unknown-field-in-group behavior becomes strict (matches QuickFIX/J) — accepted (FR-008), documented, with the extension story = dictionary currency + dialect overlays. This uses the already-supported dictionary-backed `Parser` ctor (`parser.hpp:502-540`) + the 062/063 membership/extent machinery unchanged; it does not modify the parse/membership algorithms.

## Technical Context

**Language/Version**: C++23. **Primary surface**:
- `include/fixpp/session/session.hpp` — add a stable-address `table_view` member (e.g. `std::optional<fixpp::dict::table_view> inbound_tv_;`) analogous to how `validator_` already holds one; declared so its address is stable for the session lifetime (the dict-backed `Parser` ctor stores `std::addressof(dict_metadata)`).
- `src/session/session.cpp` — `open()` (~:1160): build `inbound_tv_ = cfg_.dictionary->as_table_view()` once (dictionary is required by the null-dict guard ~:929). `parse_and_dispatch_` (`:297`, constructs the `Parser` at `:316`) is the **SINGLE parse site for BOTH admin and app** messages (admin called with `kAdminParseArena` at `:343`; app with `kInboundParseArena` at `:2709/3027/3055/3099`) — construct `Parser<access_mode::Index>{*inbound_tv_}` instead of the default, which makes **admin parses dict-backed too**. Admin is unaffected only because admin messages carry no *read* repeating groups (membership is lazy), NOT because it bypasses this change; an admin callback that reads a group would become membership-bounded (a correct change). Assess `vg_parser` (~:1869) — the opt-in validator's own parse (FR-006).
- `src/capi/message_write.cpp` (~:428-441) — `fixpp_msg_clone`: build a **clone-owned** `table_view` (stable address, owned like `owned_view_`) by copying the source view's membership via the NEW internal `MessageView` accessor (mechanism (b) — `src` holds a live dict-backed inbound view at clone time); construct the clone's `MessageView` dictionary-backed against it so a clone reads groups identically to its source (FR-007). No inbound-handle `dict_`-threading; clone stays `dict_`-free (as today at `:439`).
- `src/dictionary/reify.cpp` (~:99-121) — `owning_message_handle::view()` re-frames dict-free. **Mechanism (b)** (decided at Gate A, see Gate A section + data-model): the **same** NEW internal `MessageView` membership-copy accessor returns an **owned copy** of the source view's `table_view` (self-contained per `table_view.hpp:185-192`, spans stable-for-lifetime per `:204,221`, so it safely outlives the source session/`Dictionary` — no `shared_ptr<const Dictionary>` pin). The factory `detail::owning_message_handle_from_frame(rmv, view, mr)` (`reify.hpp:87`) copies that owned membership into the handle and re-frames dict-backed against it. **No public `reify(...)`/factory signature change** — and therefore **no codegen dispatch-emitter edit** (see below).

**Untouched by contract**: the `Parser` dict-backed ctor, `OffsetTable`/`consume_group_extent`/`nested_group_slices`, `as_table_view()`, the generated flyweights — all *algorithms* unchanged (this feature routes the existing dict-backed parse into the session/clone/reify paths). No wire-framing, no error-enum, no C-ABI **symbol** change (behavioral only). No new message families/builders/dictionary data. **The codegen dispatch-emitter (`tools/codegen/fixpp-codegen/emit_dispatch.cpp`) and the reify dispatch bridge are deliberately NOT in scope**: because reify uses mechanism (b) (an owned `table_view` copy inside the *unchanged* `owning_message_handle_from_frame` factory), the emitted `detail::owning_message_handle_from_frame(rmv, view, mr)` call sites are unchanged — no emitter edit and no forced dispatch-bridge regen (this is what makes the Codex-3 codegen-surface finding moot).

**C-ABI**: no exported C symbol / public header / error value / version change (GA-frozen 1.5.0). The change is behavioral (inbound group reads become membership-correct) — surfaced as a Behaviors & Limitations row (FR-008), not an ABI event.

**Testing**: GoogleTest under `tests/session/`, `tests/capi/`, `tests/interop/`. **The load-bearing shift**: grouped-read witnesses MUST drive a frame through **real `Session` dispatch** (and a C-ABI engine loopback), RED-first on the pre-change dict-free parse — not `Parser<Index>{dict}` unit parses (which already pass and masked this). Existing dict-backed unit tests remain the membership-logic tier. **SC-003 required witnesses (made explicit — the spike proved only a current-suite smoke, not these)**: a **validator-ON** session run; an admin / no-group regression witness (admin flows the SAME dict-backed parse site — see below); the alloc-discipline gate; ASan/UBSan/TSan lifetime for the session `inbound_tv_`, the clone-owned `table_view`, and the reify owned-membership ownership; the `tests/abi` golden unchanged; and at least one captured **group-bearing interop fixture** (or a documented reason none exists). **Target Platform**: Linux Tier-1 + Windows Tier-2 + libc++ Tier-3.

**Performance/Constraints**: `as_table_view()` built once per `open()` (not the hot path; the validator already pays it when enabled — consider sharing the two builds). Per-frame cost: root `group_context` seed (one `msg_type()` find) + membership lookups **only on an actual group read** (lazy) — admin/no-group traffic ≈ unchanged. No new **global-heap** allocation on the parse+read path: per-message membership + lazily-built nested sub-views come from the existing per-message stack arena (`kInboundParseArena=16384`, `kAdminParseArena=8192`). **Arena-fit is a witnessed requirement, not a promise** (N1): dict-backed nested reads build sub-`OffsetTable`s from the stack arena — a cost the dict-free path never incurred, now landing on BOTH arenas including the tighter 8 KiB admin arena. Add explicit witnesses (SC-004 / FR-009): a representative group-bearing **app** message parses+reads within `kInboundParseArena=16384` AND a group-bearing **admin** message within `kAdminParseArena=8192` — each asserting no heap fallback and a successful read — plus a near-cap / headroom probe, and a fail-closed test for a pathological deeply-nested message (depth/entry caps still fail closed, FR-009).

## Constitution Check

*GATE: must pass before Phase 0 (passed — research complete) and re-checked post-design.*

- **Appendix A mandatory triggers**: **Session inbound path** + **Wire/parser** (parse behavior change) → run mandatory controls: `/speckit-clarify` (done — 2 decisions recorded: strict membership accepted; clone/reify propagated), Codex **Gate A** (pending), `/speckit-analyze` (pending, after `/tasks`), user `/plan` sign-off (pending). Full **Gate B** before merge.
- **Error semantics**: no new `fixpp_error_t`; restores the documented `TYPE_MISMATCH` for scalar-as-group and membership-bounded extents. Behavior change (strict in-group) documented (FR-008).
- **C-ABI** (`[const §X.1]`, GA-frozen 1.5.0): no exported symbol/header/enum/version change; behavioral correction only (`tests/abi` golden unchanged).
- **Zero-alloc discipline** (`[const §VIII.5]`): the `table_view` build is at `open()` (not the hot path); the per-message parse+read path adds no new global-heap allocation (membership lazy; sub-views from the per-message arena) — alloc-gate-verified.
- **TDD / discriminating witnesses** (`[const Art VII §3]` TDD; `[const Art IX]` coverage/sanitizers): each behavior change lands RED→GREEN, RED-first proven **through real Session dispatch** (the dict-backed unit tests cannot prove the shipped path). Existing session/interop/C-ABI suites stay green; each intended delta is an explicit, reviewed, discriminating test edit (no silent breakage).
- **Sanitizers as real defects**: the `table_view` member lifetime (stable address bound by the Parser) + clone-owned `table_view` + reify view lifetime validated under ASan/UBSan/TSan (the dict-backed parse touches per-message arena lifetimes on the session path for the first time).
- **Cross-platform**: session parse is platform-agnostic; existing Tier-2/Tier-3 lanes cover it.

**Post-design re-check** (after Phase 1): the surface additions — a `Session` `table_view` member, a clone-owned `table_view`, and an owned membership/`table_view` copy on the reify owning handle (clone and reify both copy the source view's membership via one new internal `MessageView` accessor; no inbound-handle dict-threading) — are all internal (no C-ABI symbol/header). The behavioral change (strict in-group membership) is the intended fix + a documented B&L row. Both `/speckit-clarify` decisions (strict membership; clone/reify propagation) are resolved. The one open item is FR-006 — whether the opt-in `vg_parser` validator parse must also be dict-backed — which is an **implementation-time assessment, not a Gate-A blocker** (the validator holds its own `table_view` and walks membership itself per L-063-3, is default-off/opt-in per B-004-1, and does not gate the core fix); Decision 5 records "resolve at implement". FR-006 stays the assess-and-record task.

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
src/capi/message_write.cpp          # fixpp_msg_clone(:428): clone-owned table_view copied from the source view via the new MessageView accessor; dict-backed clone MessageView (mechanism (b), no inbound-handle dict-threading) (FR-007)
src/capi/capi_internal.hpp          # fixpp_msg: clone-owned table_view member (owned like owned_view_) (FR-007)
src/dictionary/reify.cpp            # owning_message_handle: dict-backed re-frame via owned table_view copy from the SAME accessor — mechanism (b), no factory/codegen change (FR-007)
                                    # (tools/codegen/.../emit_dispatch.cpp deliberately NOT in scope — factory signature unchanged)
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

**Structure Decision**: single-library; changes span session (the parse-site + member), the C-ABI clone, and reify — all consuming the existing dict-backed parse (clone + reify share one new internal `MessageView` membership-copy accessor). No new modules; no C-ABI symbol change; no codegen/goldens.

## Complexity Tracking

> The core (session parse dict-backed) is small. FR-007 (clone/reify propagation) is the added surface — justified by the user's clarify decision (avoid the silent clone-reads-differently footgun) and bounded below.

| Addition | Why Needed | Simpler Alternative Rejected Because |
|---|---|---|
| `Session` stable-address `table_view` member built at `open()` | The dict-backed `Parser` ctor stores a pointer to the `table_view`; it must outlive every parse. Mirrors the validator's existing owned `table_view`. | A per-message `as_table_view()` rebuild — rejected (hot-path allocation, FR-004). |
| Clone + reify both copy the source view's membership into an OWNED `table_view` via ONE new internal `MessageView` membership-copy accessor — **mechanism (b)** — copied into the **unchanged** `owning_message_handle_from_frame` factory (`reify.hpp:87`) for reify and into the clone-owned `table_view` for clone; no inbound-handle dict-threading, no public-API/codegen change | FR-007: both a clone and a reify owning handle outlive the dispatch window / session and cannot borrow the session's `table_view`; to read identically each needs an OWNED membership source. At clone/reify time the source is a live dict-backed `MessageView`, so the accessor has membership to copy. A copied `table_view` is self-contained (`table_view.hpp:185-192`), so it can be owned without a `Dictionary` pin — no public `reify(...)`/factory signature change and **no codegen emitter/dispatch-bridge edit**. | Mechanism (a) (thread a dict through the factory + pimpl + build-tree dispatch bridge + `dict::reify` entry) — rejected: it changes the factory signature → forces an `emit_dispatch.cpp` edit + forced dispatch-bridge regen (Codex 3) and a public-API/accessor for the `dict::reify` entry (Codex 2); strictly larger surface than (b). The clone-specific retain-dict-`shared_ptr`-and-rebuild path — rejected: the accessor copy from the live source view strictly dominates and drops the inbound-handle dict-threading surface. Leaving clones dict-free / documenting reify divergence / deferring to a follow-up — all rejected by the user (chose "both clone + reify in 066"). |

## Phase notes
- **Phase 0** (done): research.md — the dict-free-parse root cause + evidence; the reuse-the-existing-dict-backed-Parser mechanism; the strict-membership/Orchestra reconciliation; clone/reify lifetime; validator-parse assessment; regression-risk + real-dispatch test strategy. **Pre-Gate-A spike (research Decision 8, grade-1): the current default-config session+capi+interop ctest suite smoke-passed dict-backed (147/147, 0 regressions) — a smoke of the current suite, NOT proof of the broader regression surface (validator-on, alloc-gate, ASan/UBSan/TSan lifetime, ABI golden, group-bearing interop fixture), which SC-003's explicit witness list asserts; the 065 depth-2 typed-gap concern is disproven (3-level typed read passes dict-backed); reify's propagation surface confirmed (factory takes no dictionary → mechanism (b) copies an owned `table_view`).**
- **Phase 1** (done): data-model.md (the session table_view member + membership propagation entities), contracts/inbound-parse.md (observable before/after), quickstart.md (RED-first real-dispatch witness). Agent-context refresh.
- **Phase 2** (`/speckit-tasks`, later): tasks.md — session member + open() build; parse_and_dispatch_ dict-backed; vg_parser assessment; real-dispatch US1/US2 witnesses (RED-first); regression sweep; clone dict propagation + clone-identity witness; reify dict propagation + witness; alloc/sanitizer gates; B-066/L-066 + L-063-2 amendment; #179 amendment (comment posted 2026-07-09); close-out. Note the 065 dependency (065 re-plans + verifies through real dispatch on top of this).

## Gate A

- Round 1 applied 2026-07-09: Codex P1=2 P2=3 P3=1; Opus post-judging P1=2 P2=3 P3=4; rewrite addresses root causes RC-A (reify mechanism (b) — MessageView membership-copy accessor, no public-API/codegen change), RC-B (scope correctness to group-registering dicts + FIX4x limitation + admin-parse precision), plus vg_parser wording, Decision-8 evidence-grading + explicit SC-003 witnesses, arena-fit witnesses, and P3 doc fixes. Reviews: research/reviews/codex_066-dict-backed-inbound-parse_gate_a_review.md, research/reviews/opus_066-dict-backed-inbound-parse_gate_a_adversarial_review.md.
- Round 2 applied 2026-07-09: Codex P1=0 P2=0 P3=3; Opus post-judging P1=0 P2=0 P3=5 → CONVERGED. Swept 5 P3s: unified clone onto reify's mechanism (b) (N1 — deletes the engine.cpp/capi inbound-handle dict-threading surface; clone+reify now one mechanism), stated the accessor dict-backed precondition (N2), and 3 wording fixes (reify owned-copy not "dictionary handle"; C3 dialect-overlay backlog qualifier; stale /clarify sentence). Reviews: research/reviews/codex_066-dict-backed-inbound-parse_gate_a_2_review.md, research/reviews/opus_066-dict-backed-inbound-parse_gate_a_2_adversarial_review.md.
