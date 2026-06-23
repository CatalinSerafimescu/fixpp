# Implementation Plan: C ABI engine surface — Feature B (session lifecycle, message send, receive callback)

**Branch**: `050-c-abi-session-send-recv` | **Date**: 2026-06-24 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/050-c-abi-session-send-recv/spec.md`

## Summary

Make the Feature-A opaque handles *operable*: a pure-C consumer can create an engine, register and drive sessions, send a message, and receive inbound messages via a callback — all through `extern "C"`. The technical core is a **thin wrapping layer** over the already-shipped C++ `Engine`/`Session`/`Application` machinery (verified against real headers under `include/fixpp/session/`: `engine.hpp:213–312`, `session.hpp:128–275`, `application.hpp:46–108`, `session_config.hpp:153+`, `engine_config.hpp:125+`; impl under `src/session/`), not new session behaviour.

Three realities of the C++ surface shape the design (source-verified at plan time — see research D-1):

1. **The engine is register-then-start-once and owns no worker threads** (`engine.hpp:222` "owns NO worker threads"; `start()` "Legal to call once"). So the C-ABI engine **owns an internal io_context + worker thread(s)**, and the lifecycle is `fixpp_engine_create` → `fixpp_session_open` (= `register_session`, before start) → `fixpp_engine_start` (once) → drive → `fixpp_session_close` → `fixpp_engine_destroy` (= `co_await stop()` + join). **open ≠ connected** — establishment is asynchronous; the consumer waits on `fixpp_session_is_established`.
2. **Send takes a committed wire-frame byte span**, not a `fixpp_msg_t` — `Engine::send(SessionId, std::span<const std::byte>)`, matching `[2i §10]`. This decouples Feature B from Feature C and is testable with hand-rolled frames. Inbound, by contrast, hands the callback a `fixpp_msg_t` wrapping a `MessageView` (the handle/byte asymmetry is intentional).
3. **Receive is an engine-wide `Application` singleton** (`EngineConfig::application`), so the C-ABI installs **one internal `Application` subclass** at engine-create that holds a `SessionId → {cb, userdata}` map and trampolines `fromApp` (on-strand) to the registered C callback.

This feature also **discharges two Feature-A limitations**: L-049-1 (wire the recorded `consumer_minor` into the live `translate_for_consumer` downgrade at `fixpp_engine_create`) and L-049-2 (publish the real `FIXPP_ERR_SESSION_*` block now that `session_open`/`session_send` are the producing functions, and re-point `translate()` off the `UNKNOWN` placeholder for reachable variants). The C-ABI version takes an additive **MINOR** bump (0.2.0 → 0.3.0).

## Technical Context

**Language/Version**: C++23 (clang-22, `cppstd=23`); public C-ABI headers must also compile as C11 (`<stdint.h>`/`<stddef.h>`/`<stdbool.h>` only).
**Primary Dependencies**: none new. The `src/capi/` TUs link the existing `fixpp` engine targets (`fixpp_session` for `Engine`/`Session`/`Application`, `fixpp_core` for `error`). Internal io_context = the already-vendored asio. No third-party additions.
**Storage**: N/A directly (the session's `MessageStore` is engine-internal; send honours durable-before-transmit by reference).
**Testing**: GoogleTest (`tests/capi/`) compiled as C++ for the wrapping logic + a C-ABI round-trip smoke (SC-001) over loopback driven entirely through the C ABI (with a **test-supplied dictionary** — L-050-1; the round-trip is not fully pure-C because the dictionary producer is Feature C) + a separate **pure-C** header-compiles-as-C / 0-leak smoke (SC-003) (reuse the `tests/interop/support` loopback pattern). Synthetic-throw fault-injection fixtures for the §5.2 thunk split. nm symbol-golden + occupancy + reentrancy gates (Tier 1).
**Target Platform**: Linux/Clang (Tier 1 gating) + the per-PR nm symbol-set gate; Windows/MSVC Tier-2 consumes the same headers (export macro stays static-default-empty per L-049-3).
**Project Type**: C-ABI layer of a C++ library — `include/fix/c_api/` (public C headers) + `src/capi/` (the only `extern "C"` TU set, the AGPL-isolation boundary).
**Performance Goals**: the receive-callback dispatch path is on the session strand and inherits the `[const §VIII.5]` zero-global-heap-alloc discipline (the trampoline must not allocate). `fixpp_session_is_established` is O(1) lock-free (reads the engine's atomic reader snapshot).
**Constraints**: no C++ symbol leakage (`fixpp_capi.map` + per-PR nm gate); no exception crosses `extern "C"` (construction-time thunks catch→translate; steady-state thunks fatal-log+abort); `Session::close` is on-strand-only in v1.0 → the close thunk posts onto the session domain and bridges back synchronously; the engine dtor asserts `stopped()` → destroy must `co_await stop()` and join before `~Engine`.
**Scale/Scope**: ~9–11 new exported functions (engine create/start/destroy; session open/close/send/register_callback/is_established; the two config-builder families = create/set_*/destroy ×2) + 2 new opaque config-builder handle types + 1 new published error block (`FIXPP_ERR_SESSION_*`) + the internal `Application` trampoline + the internal event-loop owner. No new third-party dep, no codegen, no wire-format change.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- **Article X — ABI Policy (governing).**
  - §X.1 versioned contract + Codex Gate A mandatory → **Gate A in the pipeline** (ABI-surface change). MINOR bump 0.2.0→0.3.0 (FR-020).
  - §X.2 no C++ symbol leakage → `fixpp_capi.map` (`fixpp_*; local: *`) + per-PR nm gate; **every new exported symbol appended to `tests/abi/golden/fixpp_capi_symbols.txt`** (FR-018). The config-builder, callback, and lifecycle symbols are all plain `fixpp_*` `extern "C"`. **PASS by construction.**
  - §X.3 decimal boundary PoD frozen → untouched. **PASS.**
  - §X.4 bounded enum + reserved ranges + stability + audit + occupancy → the new `FIXPP_ERR_SESSION_*` block is appended at its reserved `[2i §4.3]` slot, recorded in `error_codes_v1.txt` (introducing_minor = **3**), passes the occupancy gate; **the downgrade goes live** (FR-002 records `consumer_minor` at create; codes born at minor 3 downgrade for a minor-2 consumer). Stability does not yet bind (major 0). **PASS.**
  - §X.5 per-symbol reentrancy → every new symbol carries exactly one class (FR-017), `check_capi_reentrancy.sh` gate. `is_established` = THREAD_SAFE; `send` = **THREAD_SAFE** (Gate-A-blessed, research D-7; recorded deviation from `[2i §4.10]` — see `## Gate A`; carve-out: not from the receive callback, FR-013a); blocking `close` = **SINGLE_THREAD** (non-callback/non-strand caller only); the receive callback = REQUIRES_SESSION_LOCK; `engine_create`/`engine_start` + config-builder mutators = SINGLE_THREAD. **PASS.**
  - §X.6 ABI-affecting → all four controls (`/clarify` ✔, `/analyze` pending, Codex Gate A pending, `/plan` sign-off pending). **On track.**
- **Article XI §2 — cancellation via ASIO native slot.** `fixpp_session_close` drives the engine's per-session cancellation (not a stop_token), closing the transport so a blocked idle read breaks (FR-005). **PASS by design** (reuses `Session::close` / engine teardown machinery; see [[feedback_engine_stop_must_close_transports_total_cancel_insufficient]]).
- **Article VIII §5 — exception-free steady-state hot path.** The receive trampoline + send steady-state thunk allocate nothing on the global heap and let no exception escape (escape → abort). **PASS by design**; alloc-guarded under mallocnesia (research D-6).
- **Article IX — coverage/sanitizers.** Per-PR ≥95% line / ≥85% branch on `src/capi/`; ASan/UBSan/TSan Tier-1 (the trampoline + the post-onto-strand close bridge are the threading risk surface → TSan-gated, multi-threaded harness per [[feedback_single_threaded_harness_masks_strand_races]]). **PASS (planned).**
- **No new dependency / no codegen / no new wire or `reason_class` surface.** **PASS.**

**No violations. Complexity Tracking table not required.**

**Post-Phase-1 re-check**: still no violation. Phase 1 surfaced and resolved: (a) the close-thunk on-strand bridge (post + synchronous wait) is the one place a C thread blocks on the session domain — modelled in data-model E-6, TSan-gated; (b) the `FIXPP_ERR_SESSION_*` block membership (which `session_*`/`app_*` variants are reachable through `session_open`/`session_send` and thus published vs left UNKNOWN) is enumerated in data-model E-4. No open mapping decision remains for Gate A beyond the send-reentrancy-class question (D-7) — **resolved at Gate A round 1 to `THREAD_SAFE`** (see `## Gate A`). The cross-thread blocking-bridge surface is **two** thunks (close AND send) — both TSan-gated (data-model E-6).

## Project Structure

### Documentation (this feature)

```text
specs/050-c-abi-session-send-recv/
├── plan.md              # This file
├── spec.md              # /speckit-specify + /speckit-clarify (+ plan-time refinements) output
├── research.md          # Phase 0 (this command) — D-1..D-9
├── data-model.md        # Phase 1 (this command) — E-1..E-7
├── quickstart.md        # Phase 1 (this command) — C-ABI round-trip walkthrough (test-supplied dict, L-050-1)
├── contracts/           # Phase 1 (this command)
│   ├── lifecycle-surface.md   # engine create/start/destroy + session open/close/is_established + handle keying
│   ├── send-and-receive.md    # fixpp_session_send (bytes) + register_callback + Application trampoline
│   ├── config-builders.md     # fixpp_session_config_* + fixpp_engine_config_* opaque builders
│   └── error-and-abi.md        # FIXPP_ERR_SESSION_* block + downgrade go-live + nm golden + occupancy delta
└── checklists/
    └── requirements.md   # spec-quality checklist (done; 0 markers)
```

### Source Code (repository root)

```text
include/fix/c_api/
├── engine.h        # NEW — fixpp_engine_create/start/destroy + fixpp_engine_config_* builder; reentrancy annotations
├── session.h       # NEW — fixpp_session_open/close/send/register_callback/is_established + fixpp_session_config_* builder + the receive-callback typedef
├── error.h         # EDIT — append the FIXPP_ERR_SESSION_* block (reserved [2i §4.3] slot)
├── handles.h       # EDIT — add fixpp_session_config_t / fixpp_engine_config_t opaque forward typedefs
├── version.h       # EDIT — bump FIXPP_C_ABI_VERSION_MINOR (0.2.0 → 0.3.0)
include/fix/
└── c_api.h         # EDIT — umbrella: include engine.h + session.h

src/capi/
├── engine.cpp      # NEW — engine thunks + internal io_context/worker-thread owner + the internal Application trampoline + consumer_minor wiring (L-049-1)
├── session.cpp     # NEW — session thunks (open=register, close=post-on-strand bridge, send=Engine::send, is_established=lookup+is_open, register_callback=populate trampoline map)
├── config.cpp      # NEW — the two opaque config-builder families → EngineConfig / SessionConfig
├── error.cpp       # EDIT — re-point reachable session_*/app_* arms off UNKNOWN to the new FIXPP_ERR_SESSION_* codes (L-049-2)
└── CMakeLists.txt  # EDIT — add engine.cpp/session.cpp/config.cpp to fixpp_capi_objects

tools/
├── abi_history/error_codes_v1.txt   # EDIT — append the FIXPP_ERR_SESSION_* codes (introducing_minor = 3)
tests/abi/golden/fixpp_capi_symbols.txt  # EDIT — append the new exported symbols

tests/capi/
├── lifecycle_test.cpp   # NEW — create→open→start→is_established→close→destroy; register-after-start rejected; double-destroy idempotent; SC-007 close-breaks-blocked-read (real socket, TSan)
├── send_recv_test.cpp   # NEW — HEADLINE: two C-ABI engines (initiator+acceptor) over loopback plaintext TCP; bidirectional conversation; reply from a drain thread (D-10 supported path); inbound handle invalid after return (ASan); test-supplied dict (L-050-1 — productive loading is Feature C). Strategy = research D-11
├── error_block_test.cpp # NEW — session_*/app_* reachable variants → published codes; downgrade live (minor-3 code → UNKNOWN for minor-2 consumer)
├── thunk_split_test.cpp # NEW — synthetic-throw: construction (create/open/start)→*_CONFIG no abort; steady-state (send)→abort (SIGABRT trap)
└── CMakeLists.txt       # EDIT — register the new targets
```

**Structure Decision**: Single C-ABI layer — `include/fix/c_api/` public headers + `src/capi/` `extern "C"` TUs, mirroring Feature A. Split into `engine.{h,cpp}` / `session.{h,cpp}` / `config.cpp` by lifecycle owner; the internal `Application` trampoline + event-loop owner live in `engine.cpp` (engine-scoped). No new top-level module.

## Complexity Tracking

> No Constitution Check violations — table not required.

The one structural novelty (the C-ABI owning an internal io_context + worker thread, absent from Feature A) is **not** a constitution violation but is the load-bearing risk; it is isolated to `engine.cpp` and justified by the verified `engine.hpp:222` "owns NO worker threads" contract — a C consumer has no asio executor to supply, so the boundary must own one. Recorded in research D-2.

## Gate A

- Round 1 applied 2026-06-24: Codex P1=1 P2=6 P3=0; Opus post-judging P1=3 P2=6 P3=3; rewrite addresses root causes #1 reentrancy-deviation, #2 dictionary/SC-001, #3 stale-text, #4 trampoline-exception, #5 cite-hygiene + New-P1 destroy-abort + 3 New-P2 + 2 P3. Reviews: research/reviews/codex_050-c-abi-session-send-recv_gate_a_review.md, research/reviews/opus_050-c-abi-session-send-recv_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-24: Codex P1=0 P2=2 P3=2; Opus post-judging P1=0 P2=3 P3=2; rewrite addresses the E-4/L-049-2 reconciliation (LEAVE threading-block arms; reconcile discharge sentence + D-8 scope + D-11 oracle off a single reachable-arm enumeration) + 2 mechanical stale-text P2 + 2 P3. Reviews: research/reviews/codex_050-c-abi-session-send-recv_gate_a_2_review.md, research/reviews/opus_050-c-abi-session-send-recv_gate_a_2_adversarial_review.md.
- Round 3 applied 2026-06-24 — **orchestrator-inline (rewrite budget of 2 exhausted; per the user's "apply mechanical fixes yourself" authorization)**: Codex P1=0 P2=2; Opus census post-judging P1=0 P2=4 — all completeness additions to the E-4 reachable-arm set + the send-contract return list, **no design change, no new `fixpp_error_t`, no `translate()` re-point** (the "exactly 5 newly-published" invariant holds). A round-3 source census (Opus, independently confirmed by the orchestrator reading `src/session/session.cpp` + `seqnum_manager.cpp`) caught arms that R1/R2 both missed AND a **round-1-rewrite-introduced phantom**: the round-1 E-4 had listed `store_io_failure → STORE_RUNTIME` as send-reachable, but on the `Session::send` path `store_then_emit` **swallows** store I/O errors (I-07 logged-then-proceed, `session.cpp:4790`) — only `store_seqnum_overflow` (counter at `seqnum_max`, `assign_outbound`) is the reachable store-domain send arm. Edits: E-4 += `wire_frame_too_large` (30 → WIRE_LIMIT_EXCEEDED, existing), `store_io_failure`-row retargeted to `store_seqnum_overflow` (60 → STORE_RUNTIME, existing), `session_already_closed` (52) gains its send-path TOCTOU producer; send contract return-list += `APP_PAYLOAD_MALFORMED` + `WIRE_LIMIT_EXCEEDED` + `THREAD_SESSION_LIFECYCLE`; **US2/AC3 corrected** to witness `store_seqnum_overflow` (realizable: restore outbound counter at `seqnum_max`) instead of the I-07-swallowed I/O failure, and the swallow recorded as **L-050-3**. Reviews: research/reviews/codex_050-c-abi-session-send-recv_gate_a_3_review.md, research/reviews/opus_050-c-abi-session-send-recv_gate_a_3_adversarial_review.md. **Terminal-completeness statement (Opus census, all 6 producers traced to every return site): `engine_create` + config-builder setters → zero `core::error`; `register_session`/`start`/`send`/`close` fully traced; the reachable set is now exact.**

### Round 1 — deviations

- **`fixpp_session_send` reentrancy class = `FIXPP_THREAD_SAFE` (deviation from `[2i §4.10]:1176`).** `[2i §4.10]`'s reentrancy table illustratively lists `fixpp_session_send` under `FIXPP_REQUIRES_SESSION_LOCK`. That example is **stale**: `Engine::send` is any-thread-safe (`include/fixpp/session/engine.hpp:241-267` — enrollment gate + shared_ptr keepalive + re-entrant on-strand enqueue, no deadlock), and a pure-C consumer has no asio executor / strand handle, so `REQUIRES_SESSION_LOCK` would be unsatisfiable from a normal C thread. Feature B therefore **blesses `THREAD_SAFE`** for `fixpp_session_send`, with an explicit carve-out: it must NOT be called from inside the receive callback, where the blocking C wrapper deadlocks (FR-013a / research D-10). The any-thread-ness is a property of `Engine::send`-the-awaitable; the blocking wrapper keeps the on-strand-callback hazard. **The `[2i §4.10]:1176` `fixpp_session_send` example is flagged for a future `[2i]` erratum; `[2i]` is NOT reopened or edited by this feature.** (RC#1; FR-017; contracts/send-and-receive.md:4; contracts/lifecycle-surface.md:22.)

### Round 1 — dictionary decision (RC#2 / SC-001)

- **Option taken: DESCOPE (option B).** SC-001's pure-C round-trip is unachievable as originally scoped: a `SessionConfig` requires a non-null `dictionary` (`include/fixpp/session/session_config.hpp:180`) and `Session::open()` rejects null **unconditionally with no engine fallback** (`src/session/session.cpp:925-931` — verified; the clock axis has an `engine.clock` fallback, the dictionary axis does not). A Gate-A Explore sweep confirmed the **only** `Dictionary` producers are C++ `XmlLoader::load(path)` / `load_from_string(xml)`; there is **no built-in / version-keyed dictionary factory** (`version_registry` is a lookup over pre-loaded dictionaries, not a producer), and the file-loading C-ABI surface (`fixpp_dict_load_*`) is **Feature C**. A cheap pure-C selector would require pulling Feature C forward or shipping a net-new embedded-dict-by-version mechanism — both exceed "don't pull Feature C forward" / Simplicity First. **Decision:** descope SC-001 to "a C-ABI round-trip with a test-supplied dictionary," record the round-trip as blocked on Feature C (**L-050-1**, spec Limitations + research D-3a), and reword SC-001 so it no longer over-claims "pure-C." (RC#2.)

### Round 2 — decisions

- **L-049-2 discharge outcome (NOT empty, but narrower than the round-1 E-4 implied).** Enumerated from source (`src/capi/error.cpp` + `Engine::send`/`Session::send`/`Session::open`/`register_session`/`start` impls — see data-model E-4 / research D-8), the reachable arms that 049 left at `FIXPP_ERR_UNKNOWN` and that Feature B therefore **newly publishes** are exactly **5**: `session_invalid_argument` (119), `session_invalid_state_for_send` (77) — session-PROTOCOL block — plus `app_do_not_send` (129), `app_callback_threw` (130), `app_payload_malformed` (131) — app block. (131 was **missing** from the round-1 E-4 table; it is reachable because the 020 fail-closed opaque-payload validation runs on the byte-span `Engine::send` path — `session.cpp:4112-4330`.) Feature B publishes a `FIXPP_ERR_SESSION_*` block for 77/119 and app codes for 129/130/131; L-049-2's "re-point reachable arms off UNKNOWN" discharge covers exactly these and no more.

- **LEAVE decision — the slot-47-55 threading-block lifecycle/config arms STAY on their existing THREAD code; they are NOT re-pointed to a SESSION block.** `session_already_open` (51) / `session_already_closed` (52) → `FIXPP_ERR_THREAD_SESSION_LIFECYCLE`; `invalid_session_config` (53) / `executor_not_serialised` (48) / `clock_not_set` (54) → `FIXPP_ERR_THREAD_CONFIG` (`error.cpp:92-104`). Rationale: (a) **L-049-2 scope** — L-049-2 only covered arms that 049 left *at UNKNOWN* (the session-PROTOCOL block 66-77/116-121 + app/log/otel; 049 census-ground-truth confirms the session set is 66-77/116-121); these threading-block arms were already `THREAD_*`, so L-049-2 never covered them and there is nothing to "re-point off UNKNOWN." (b) **Taxonomic consistency** — they mirror `error.hpp`'s slot-47-55 "threading" block and already have a published home; minting a parallel `FIXPP_ERR_SESSION_*` code would duplicate it. The round-1 E-4 mislabelled these four as "(new)/off-UNKNOWN placeholders" against the shipped `error.cpp`; corrected in this rewrite (data-model E-4 + research D-8 + the D-11 oracle now agree, all driven by the single source enumeration).
