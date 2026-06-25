# Implementation Plan: C ABI message surface — Feature C (field/group accessors, outbound construct + commit, toApp hook) + the [2i §4.3] session/app error-block amendment

**Branch**: `051-c-abi-message-accessors` | **Date**: 2026-06-24 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/051-c-abi-message-accessors/spec.md`

## Summary

Make the Feature-B inbound `fixpp_msg_t` *readable* and give pure-C consumers a way to *construct* outbound messages — closing the last C-ABI gap before the `0→1` GA freeze and unblocking the Python bindings (PY-001..005). The work splits into three shapes, **all verified against real headers at plan time** (research D-1):

1. **Inbound read (CA-008 / CA-010-read) = thin thunks.** `wire::MessageView<Index>::get(uint16_t tag) → expected_t<field_view>` (runtime, tag-keyed) and the runtime `OffsetTable::group_slices(NoTag)` under the templated `group<>()` already exist (`parser.hpp:200,233`); the C accessors thunk into them. Group entries are walked per-instance by tag; nested groups via the instance sub-walk.
2. **Outbound construct + commit (CA-009 / CA-010-write) = NEW in-arena surface.** `wire::Writer` is a **full-frame** serialiser (backpatches `9=`/appends `10=`, `writer.hpp:106`) — it produces exactly what `Engine::send` *rejects* (`Session::send_impl` splices the app-payload between a stamped header/trailer and rejects payloads carrying framing tags `8/9/34/49/52/56/10`, `session.cpp:4100,4157`). So `fixpp_msg_commit` does **not** wrap `Writer`; the outbound `fixpp_msg_t` is a **mutable in-arena ordered field/group accumulator** (session per-message arena, `session.hpp:189` `session_arena()`) that `fixpp_msg_commit` serialises into a valid wire-order app-payload (`35=<type>` first, `digit-tag=non-empty-value\x01` fields, SOH-terminated, no framing tags, groups in dictionary-grammar order). `set_*` rejects framing tags at set-time (fail-fast).
3. **toApp send-callback hook (Group 3) = NEW trampoline.** `CapiApplication` gains a `toApp` override (mirroring its `fromApp`) routing to a registered C send-callback; `Session::send_impl` already fires `toApp` on the originate path (`session.cpp:278`). The C verdict (send / veto / error) maps to `toApp` returning `{}` / `unexpected(app_do_not_send)` / `unexpected(app_callback_threw)`. This makes **all five** session/app arms end-to-end-reachable from pure C (user decision 2026-06-24).

**Folded in per Article XX:** the `[2i §4.3]` amendment publishes a **dedicated Phase-4 session/app + message-construction error block at `[1400,1499]`** (research D-6, RULED at Gate A round 1: session/app are C-ABI-boundary *domain* failures, not boundary sentinels — a dedicated block avoids permanently relabelling the `[0,99]` sentinel range; `[1400,1499]` is the last 100-wide block in the budget, verified free, no collision with `[1300,1399]` wire-format growth), mints **6** codes — 1400–1404 mapping the C++ ordinals `session_invalid_argument`(119)/`session_invalid_state_for_send`(77)/`app_do_not_send`(129)/`app_callback_threw`(130)/`app_payload_malformed`(131), plus `FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN`(1405) for the framing-tag set reject — re-points the 5 mapped `translate()` arms off `FIXPP_ERR_UNKNOWN`, **replaces the scalar `kIntroducingMinor` with a per-code introducing-minor table** (existing codes keep minor 2, the 6 new get minor 4 — so a `consumer_minor=3` engine does NOT downgrade existing codes), and makes the 050-deferred SC-004 minor-gated downgrade live. **Discharges L-050-4 + L-049-2 (session/app arms; log/otel stay deferred-by-design).** Additive **MINOR** bump 0.3.0 → 0.4.0.

## Technical Context

**Language/Version**: C++23 (clang-22, `cppstd=23`); public C-ABI headers must also compile as C11 (`<stdint.h>`/`<stddef.h>`/`<stdbool.h>` only).
**Primary Dependencies**: none new. `src/capi/` TUs link the existing `fixpp` targets (`fixpp_wire` for `MessageView`/`OffsetTable`/`Writer` primitives, `fixpp_session` for `Session`/`Application`, `fixpp_core` for `error`/`decimal`). No third-party additions, no codegen.
**Storage**: N/A. The outbound accumulator uses the session's per-message PMR arena (`Session::session_arena()`); no global heap.
**Testing**: GoogleTest (`tests/capi/`) for the wrapping logic + a **pure-C** read+write round-trip smoke (SC-001) over loopback (test-supplied dictionary, inherited L-050-1) + group read/write (SC-002) + the §9 allocation guard (counting-resource + mallocnesia, SC-003) + the 5-arm error-block witness incl. the toApp arms (SC-004) + the `fixpp_msg_clone` cross-strand seam #13 (SC-006) + the outbound-tombstone-on-session-close seam (FR-009a). nm symbol-golden + occupancy + reentrancy gates (Tier 1).
**Target Platform**: Linux/Clang (Tier-1 gating) + per-PR nm symbol-set gate; Windows/MSVC Tier-2 + libc++ Tier-3 consume the same headers (export macro stays static-default-empty per L-049-3). Run-tier{1,2,3} AFTER merge (last-C-ABI-feature milestone).
**Project Type**: C-ABI layer of a C++ library — `include/fix/c_api/` (public C headers) + `src/capi/` (the only `extern "C"` TU set, the AGPL-isolation boundary).
**Performance Goals**: read accessors ≤ 50 ns p99 warm-cache + ≤ 10 ns delta vs the C++ accessor (`[2i §4.6]`); setters ≤ 100 ns p99 (`set_int`/`set_double`), ≤ 200 ns (`set_string` < 64 B) (`[2i §4.7]`); `fixpp_msg_clone` ≤ 1 µs warm-cache for ~200 B (`[2i §9]` seam #13). Read + set paths zero-global-heap.
**Constraints**: no C++ symbol leakage (`fixpp_capi.map` + per-PR nm gate); no exception crosses `extern "C"` (construction thunks `create_outbound`/`clone` catch→translate; steady-state accessors/setters/commit fatal-log+abort per `[2i §5.2]`); the outbound `fixpp_msg_t` is tombstoned on session close (FR-009a, reusing the 050 handle-tag/tombstone discipline); the toApp trampoline runs on the engine single-thread executor (`application.hpp:8`) and must not allocate on the global heap.
**Scale/Scope**: **exactly 33** new exported functions + 6 new error codes + 3 new opaque handle types (`fixpp_group_t`, `fixpp_group_builder_t`, `fixpp_entry_t`). Read (CA-008): `get_string/bytes/int/double/decimal` + `has_tag` + `version` + `get_msg_type` = 8. Outbound (CA-009): `create_outbound/destroy/clone` + `set_string/bytes/int/double/decimal` + `remove_tag` + `commit` = 10. Group read (CA-010): `get_group` + `group_get_field_{string,int,double,decimal}` + `get_nested_group` = 6. Group build (CA-010): `msg_group_begin` + `group_builder_add_entry` + `entry_set_{string,int,double,decimal}` + **`entry_group_begin`** (nested, NEW per #6) + `msg_group_end` = **8**. Send callback (Group 3): `register_send_callback` = 1 (+ callback typedef + the closed `fixpp_toapp_verdict` enum — types, NOT exported functions). Total = 8+10+6+8+1 = **33**. The 6 error `#define`s and the verdict enum/callback typedef are NOT exported symbols and do NOT enter the count. **The nm golden (`tests/abi/golden/fixpp_capi_symbols.txt`) is the single source of truth for the exact symbol set.** **Amends `[2i §4.3]`** (the only feature that reopens a signed-off Phase-2 doc — Article XX, folded into Gate A).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- **Article X — ABI Policy (governing).**
  - §X.1 versioned contract + Codex Gate A mandatory → **Gate A in the pipeline** (ABI-surface change AND a signed-off-design amendment). MINOR bump 0.3.0→0.4.0 (FR-019).
  - §X.2 no C++ symbol leakage → `fixpp_capi.map` (`fixpp_*; local: *`) + per-PR nm gate; every new exported symbol appended to `tests/abi/golden/fixpp_capi_symbols.txt` (FR-021). All new symbols are plain `fixpp_*` `extern "C"`. **PASS by construction.**
  - §X.3 decimal boundary PoD frozen → `fixpp_msg_get_decimal`/`set_decimal` consume/produce the 2a `fixpp_decimal_t` PoD unchanged (`[2a §5.1]`). **PASS.**
  - §X.4 bounded enum + reserved ranges + stability + audit + occupancy → **6 new codes in the dedicated Phase-4 block `[1400,1499]`** (1400–1405; FR-013/014, research D-6, RULED at Gate A round 1); `error_codes_v1.txt` append (6 rows, introducing-minor 4); occupancy gate Check A gains the **new Phase-4-owned accounting term** (+6 `EXPECTED` entries) + the `[2i §1.1]` published-block sites swept in one pass (FR-016); the `[0,99]` count stays 11/8 and the prior-doc `97` total + Check B's 8 source-domain counts are UNCHANGED (a dedicated block adds no sentinel-range contamination); additive only — no slot re-defined. **PASS (amendment reviewed at Gate A on the real `[2i §4.3]` diff).**
  - §X.5 per-symbol reentrancy → every new symbol carries exactly one class (FR-018), `check_capi_reentrancy.sh` gate. The shared `fixpp_msg_get_*` reads / setters / group cursors / toApp callback = `FIXPP_REQUIRES_SESSION_LOCK` (the **single conservative class**, matching the inherited `[2i §4.6]` annotation — NOT edited); `fixpp_msg_version`/`fixpp_msg_destroy` = `FIXPP_THREAD_SAFE`; `fixpp_msg_clone` = `REQUIRES_SESSION_LOCK` on the source; `register_send_callback` = SINGLE_THREAD (pre-start). The **detached-clone-read `FIXPP_THREAD_SAFE` property is a documented runtime/handle-state guarantee OUTSIDE the per-symbol gate** — the static gate checks one annotation per declaration and cannot distinguish a clone read from a flyweight read on the same symbol; zero new symbols, gate unchanged. **PASS.**
  - §X.6 ABI-affecting → controls: `/clarify` ✔, `/analyze` pending, Codex Gate A pending (folds the §XX amendment review), `/plan` sign-off pending. **On track.**
- **Article XX — amendment process.** This feature **reopens the signed-off `[2i §4.3]`** (which 050's Gate A left closed, CHK030). Per the 043 precedent the design-doc amendment review is **folded into this feature's Gate A** (FR-013); the actual `[2i §4.3]` diff is the review artifact. **PASS by process** (Gate A is the venue).
- **Article VIII §5 — exception-free zero-alloc steady-state hot path.** Read accessors, setters, group cursors, commit, and the toApp trampoline allocate nothing on the global heap (arena-only) and let no exception escape (escape → abort). **PASS by design**; alloc-guarded under mallocnesia + counting-resource (research D-11; [[feedback_tracking_pmr_resource_false_pass]]).
- **Article IX — coverage/sanitizers.** Per-PR ≥95% line / ≥85% branch on `src/capi/`; ASan/UBSan/TSan Tier-1. The outbound-tombstone-on-session-close path and the toApp trampoline are the lifecycle/threading risk surface → sanitizer-gated, multi-threaded harness ([[feedback_single_threaded_harness_masks_strand_races]]; [[feedback_cabi_handle_destroy_needs_tombstone]]). **PASS (planned).**
- **No new dependency / no codegen / no new wire-format or `reason_class` surface.** The new error codes are C-ABI-only (the C++ `core::error` ordinals already exist). **PASS.**

**No violations. Complexity Tracking table not required** (the one structural novelty — a net-new outbound in-arena accumulator + serialiser — is isolated to `src/capi/` and justified below).

**Post-Phase-1 re-check**: still no violation. Phase 1 surfaced and resolved: (a) the outbound accumulator's emission-order contract (data-model E-3) is pinned to wire/grammar order because `send_impl` splices (D-2); (b) the dictionary needed for `TYPE_MISMATCH`/`DICT_CONFIG` is the threaded `opaque_dict`/`classify_fn` on the inbound view and the session's `Dictionary` on the outbound path (D-5); (c) the toApp `app_callback_threw` verdict maps to `unexpected(app_callback_threw)` (terminal-close), NOT a generic error (which `application.hpp:102` says would `abort`) (D-8). No open mapping decision remains for Gate A beyond the `[2i §4.3]` numeric-range diff (D-6, RULED `[1400,1499]` at round 1) and the 6-distinct-codes-vs-coalesce defense (D-6).

## Project Structure

### Documentation (this feature)

```text
specs/051-c-abi-message-accessors/
├── plan.md              # This file
├── spec.md              # /speckit-specify + /speckit-clarify (+ plan-time refinements) output
├── research.md          # Phase 0 (this command) — D-1..D-13
├── data-model.md        # Phase 1 (this command) — E-1..E-9
├── quickstart.md        # Phase 1 (this command) — pure-C read+write round-trip walkthrough
├── contracts/           # Phase 1 (this command)
│   ├── message-read.md          # CA-008 accessors + CA-010 group read (thin thunks)
│   ├── message-write.md         # CA-009 construct/set/commit + CA-010 group build (in-arena accumulator)
│   ├── toapp-callback.md        # register_send_callback + CapiApplication::toApp trampoline + verdict contract
│   └── error-block-amendment.md # the [2i §4.3] diff: dedicated Phase-4 [1400,1499] block + translate()/audit/occupancy co-update
└── checklists/
    └── requirements.md   # spec-quality checklist (done; FR-012 + toApp + tombstone + clone resolved)
```

### Source Code (repository root)

```text
include/fix/c_api/
├── message.h      # NEW — CA-008 read accessors + CA-009 setters/create_outbound/clone/destroy/commit + CA-010 group read+build + fixpp_group_t/fixpp_group_builder_t/fixpp_entry_t opaque typedefs; reentrancy annotations
├── session.h      # EDIT — add fixpp_session_register_send_callback + the toApp callback typedef + verdict enum (Group 3)
├── error.h        # EDIT — add the 6 FIXPP_ERR_SESSION_*/FIXPP_ERR_APP_*/FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN codes in the dedicated Phase-4 block [1400,1499]
├── version.h      # EDIT — bump FIXPP_C_ABI_VERSION_MINOR (0.3.0 → 0.4.0)
include/fix/
└── c_api.h        # EDIT — umbrella: include message.h (session.h already included)

src/capi/
├── message_read.cpp    # NEW — CA-008 + CA-010-read thunks over MessageView::get / OffsetTable::group_slices
├── message_write.cpp   # NEW — outbound in-arena accumulator + set_* + commit (serialise to app-payload) + clone + destroy + group builder
├── session.cpp         # EDIT — fixpp_session_register_send_callback (populate the toApp trampoline slot)
├── engine.cpp          # EDIT — CapiApplication::toApp override (trampoline to the registered C send-cb; verdict→expected_t)
├── error.cpp           # EDIT — re-point the 5 mapped translate() arms off UNKNOWN; add 6 strerror entries (incl. 1405, no translate() arm); REPLACE the scalar kIntroducingMinor with a per-code introducing-minor lookup (existing codes minor 2, the 6 new minor 4)
├── capi_internal.hpp   # EDIT — outbound fixpp_msg accumulator struct + handle tag_ + a std::weak_ptr<SessionLiveness> session-validity token (E-9); fixpp_session shell gains the strong shared_ptr<SessionLiveness> reset on EVERY arena-teardown path (fixpp_session_close AND fixpp_engine_destroy's sessions_ loop AND internal session removal — not close() only); SessionSlot gains send_cb
└── CMakeLists.txt      # EDIT — add message_read.cpp / message_write.cpp to fixpp_capi_objects

.specify/
└── 2i-capi.md          # EDIT (Article XX amendment) — §4.3 [1400,1499] dedicated Phase-4 session/app+msg-construction block + §1.1 magnitude table (new Phase-4 row) + §1.1 layout (new [1400,1499] row) + §1.1 reserved-blocks prose (mark spent) + §4.3 inline #define block. [0,99] count + §6.5 prior-doc total + Appendix D.2 UNCHANGED. (§4.7 is NOT edited — the stale-send-prose reconciliation is a LOCAL Gate-A deviation recorded in this feature's spec/contracts per FR-008a.)

tools/
├── abi_history/error_codes_v1.txt   # EDIT — append 6 rows 1400-1405 (introducing-minor 4); 1405 = C-ABI construction reject, no C++ ordinal
├── check_capi_occupancy.sh          # EDIT — Check A EXPECTED map +6 entries (1400-1405) = the NEW Phase-4-owned accounting term; Check B (8 prior-doc source-domain counts) + prior-doc 97 total UNCHANGED
tests/abi/golden/fixpp_capi_symbols.txt  # EDIT — append the exactly 33 new exported symbols (single source of truth for the symbol set)

tests/capi/
├── message_read_test.cpp    # NEW — US1/US3: read string/bytes/int/double/decimal/msg_type/has_tag; absent→TAG_NOT_FOUND, wrong-flavour→TYPE_MISMATCH, non-numeric int→WIRE_INVALID_FRAME; group read incl. nested + INDEX_OUT_OF_RANGE; alias-not-copy + zero-global-heap (mallocnesia, SC-003)
├── message_write_test.cpp   # NEW — US2/US4: create_outbound→set_*→commit→send round-trip (SC-001); group build incl. NESTED (fixpp_entry_group_begin, LIFO close; out-of-order close→INVALID_HANDLE) (SC-002); set framing-tag→FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN; commit over-cap→WIRE_LIMIT_EXCEEDED; set on inbound→INVALID_HANDLE; destroy idempotent; outbound-tombstone with the ARENA ACTUALLY RECLAIMED — BOTH orderings: (a) close→set_*, AND (b) engine-destroy WITHOUT prior close→set_* (the discriminating path that genuinely frees session_arena_), set_*→INVALID_HANDLE via the lazy weak-ptr token, under ASan+TSan (FR-009a); commit→send→immediate-destroy ASan seam (Codex #4)
├── msg_clone_cross_strand_test.cpp # NEW — [2i §9] seam #13 (SC-006): clone read on strand B after source window closed; clone reads THREAD_SAFE (documented runtime property, not gate-enforced)
├── toapp_callback_test.cpp  # NEW — US6: FIXPP_TOAPP_SEND→transmit; FIXPP_TOAPP_VETO→APP_DO_NOT_SEND; FIXPP_TOAPP_ERROR (and an out-of-range verdict)→APP_CALLBACK_THREW; outbound readable as a FRAMED view (8/9/34/49/52/56/10 visible)
├── error_block_test.cpp     # NEW — US5/SC-004: all 5 arms → published 1400-1404 codes (not UNKNOWN); 1405 framing reject; strerror non-empty; per-code minor downgrade BOTH ways at consumer_minor=3 — a NEW minor-4 code → UNKNOWN AND an EXISTING minor-2 code (e.g. DICT_CONFIG) SURVIVES (per-code-table witness)
└── CMakeLists.txt           # EDIT — register the new targets
```

**Structure Decision**: Single C-ABI layer, mirroring 049/050 — `include/fix/c_api/` public headers + `src/capi/` `extern "C"` TUs. New `message.{h}` + `message_read.cpp`/`message_write.cpp`; `session.h`/`engine.cpp`/`error.cpp` edited for the toApp hook + error block. The `[2i §4.3]` amendment is the one cross-cutting edit outside `src/capi/` (reviewed at Gate A). No new top-level module.

## Complexity Tracking

> No Constitution Check violations — table not required.

The load-bearing novelty is the **net-new outbound in-arena field/group accumulator + app-payload serialiser** (`message_write.cpp`). It is not a constitution violation but is the primary risk: it cannot reuse `wire::Writer` (which frames the message — 8/9/…/10 — and would be rejected by `Session::send_impl`'s anti-framing validation), so Feature C authors a small serialiser that emits a valid wire-order app-payload directly into the session arena. Isolated to `src/capi/message_write.cpp`; the emission-order + framing-tag-rejection contract is pinned in data-model E-3 and witnessed by the SC-001 round-trip against a live peer. Recorded in research D-2.

## Gate A

- Round 1 applied 2026-06-24: Codex P1=2 P2=6 P3=0; Opus post-judging P1=3 P2=6 P3=2; rewrite addresses root causes #1 (error-block→[1400,1499]), #2 (conservative reentrancy + runtime-doc clone exemption), #3 (pinned construction-reject code + per-code introducing-minor table + SC-004 old-code-survival witness), #4 (lazy session-close tombstone token + arena-reclaimed sanitizer seam); + nested-entry group symbols added per user decision (FR-012 MUST); closed toApp verdict enum; framed-toApp-view contract; exact symbol count; commit-span dependency note; checklist refresh. Reviews: research/reviews/codex_051-c-abi-message-accessors_gate_a_review.md, research/reviews/opus_051-c-abi-message-accessors_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-24: Codex P1=1 P2=1 P3=2; Opus post-judging P1=0 P2=1 P3=2; doc-only convergence sweep — (1) research D-8 + data-model E-6 rewritten to the closed fixpp_toapp_verdict enum [P2]; (2) spec.md clone-reentrancy clarification reworded to single-conservative-class + runtime-doc model [P3]; (3) [2i §4.7] stale-send-prose reconciliation recorded as a LOCAL Gate-A deviation (deleted §4.7 co-update row + plan EDIT clause; [2i §4.7] NOT edited) [P3]. All four round-1 root causes independently re-verified resolved from source. Reviews: research/reviews/codex_051-c-abi-message-accessors_gate_a_2_review.md, research/reviews/opus_051-c-abi-message-accessors_gate_a_2_adversarial_review.md.
