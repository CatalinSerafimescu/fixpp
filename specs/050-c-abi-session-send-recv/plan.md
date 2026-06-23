# Implementation Plan: C ABI engine surface — Feature B (session lifecycle, message send, receive callback)

**Branch**: `050-c-abi-session-send-recv` | **Date**: 2026-06-24 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/050-c-abi-session-send-recv/spec.md`

## Summary

Make the Feature-A opaque handles *operable*: a pure-C consumer can create an engine, register and drive sessions, send a message, and receive inbound messages via a callback — all through `extern "C"`. The technical core is a **thin wrapping layer** over the already-shipped C++ `Engine`/`Session`/`Application` machinery (verified against real headers: `engine.hpp:213–312`, `session.hpp:128–275`, `application.hpp:46–108`, `session_config.hpp:153+`, `engine_config.hpp:125+`), not new session behaviour.

Three realities of the C++ surface shape the design (source-verified at plan time — see research D-1):

1. **The engine is register-then-start-once and owns no worker threads** (`engine.hpp:222` "owns NO worker threads"; `start()` "Legal to call once"). So the C-ABI engine **owns an internal io_context + worker thread(s)**, and the lifecycle is `fixpp_engine_create` → `fixpp_session_open` (= `register_session`, before start) → `fixpp_engine_start` (once) → drive → `fixpp_session_close` → `fixpp_engine_destroy` (= `co_await stop()` + join). **open ≠ connected** — establishment is asynchronous; the consumer waits on `fixpp_session_is_established`.
2. **Send takes a committed wire-frame byte span**, not a `fixpp_msg_t` — `Engine::send(SessionId, std::span<const std::byte>)`, matching `[2i §10]`. This decouples Feature B from Feature C and is testable with hand-rolled frames. Inbound, by contrast, hands the callback a `fixpp_msg_t` wrapping a `MessageView` (the handle/byte asymmetry is intentional).
3. **Receive is an engine-wide `Application` singleton** (`EngineConfig::application`), so the C-ABI installs **one internal `Application` subclass** at engine-create that holds a `SessionId → {cb, userdata}` map and trampolines `fromApp` (on-strand) to the registered C callback.

This feature also **discharges two Feature-A limitations**: L-049-1 (wire the recorded `consumer_minor` into the live `translate_for_consumer` downgrade at `fixpp_engine_create`) and L-049-2 (publish the real `FIXPP_ERR_SESSION_*` block now that `session_open`/`session_send` are the producing functions, and re-point `translate()` off the `UNKNOWN` placeholder for reachable variants). The C-ABI version takes an additive **MINOR** bump (0.2.0 → 0.3.0).

## Technical Context

**Language/Version**: C++23 (clang-22, `cppstd=23`); public C-ABI headers must also compile as C11 (`<stdint.h>`/`<stddef.h>`/`<stdbool.h>` only).
**Primary Dependencies**: none new. The `src/capi/` TUs link the existing `fixpp` engine targets (`fixpp_session` for `Engine`/`Session`/`Application`, `fixpp_core` for `error`). Internal io_context = the already-vendored asio. No third-party additions.
**Storage**: N/A directly (the session's `MessageStore` is engine-internal; send honours durable-before-transmit by reference).
**Testing**: GoogleTest (`tests/capi/`) compiled as C++ for the wrapping logic + a **pure-C** round-trip smoke (SC-001) over loopback (reuse the `tests/interop/support` loopback pattern, but driven entirely through the C ABI). Synthetic-throw fault-injection fixtures for the §5.2 thunk split. nm symbol-golden + occupancy + reentrancy gates (Tier 1).
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
  - §X.5 per-symbol reentrancy → every new symbol carries exactly one class (FR-017), `check_capi_reentrancy.sh` gate. `is_established` = THREAD_SAFE; `send` = REQUIRES_SESSION_LOCK (conservative default; THREAD_SAFE candidacy → Gate A, research D-7); the callback = REQUIRES_SESSION_LOCK; `engine_create`/`engine_start` + config-builder mutators = SINGLE_THREAD. **PASS.**
  - §X.6 ABI-affecting → all four controls (`/clarify` ✔, `/analyze` pending, Codex Gate A pending, `/plan` sign-off pending). **On track.**
- **Article XI §2 — cancellation via ASIO native slot.** `fixpp_session_close` drives the engine's per-session cancellation (not a stop_token), closing the transport so a blocked idle read breaks (FR-005). **PASS by design** (reuses `Session::close` / engine teardown machinery; see [[feedback_engine_stop_must_close_transports_total_cancel_insufficient]]).
- **Article VIII §5 — exception-free steady-state hot path.** The receive trampoline + send steady-state thunk allocate nothing on the global heap and let no exception escape (escape → abort). **PASS by design**; alloc-guarded under mallocnesia (research D-6).
- **Article IX — coverage/sanitizers.** Per-PR ≥95% line / ≥85% branch on `src/capi/`; ASan/UBSan/TSan Tier-1 (the trampoline + the post-onto-strand close bridge are the threading risk surface → TSan-gated, multi-threaded harness per [[feedback_single_threaded_harness_masks_strand_races]]). **PASS (planned).**
- **No new dependency / no codegen / no new wire or `reason_class` surface.** **PASS.**

**No violations. Complexity Tracking table not required.**

**Post-Phase-1 re-check**: still no violation. Phase 1 surfaced and resolved: (a) the close-thunk on-strand bridge (post + synchronous wait) is the one place a C thread blocks on the session domain — modelled in data-model E-6, TSan-gated; (b) the `FIXPP_ERR_SESSION_*` block membership (which `session_*`/`app_*` variants are reachable through `session_open`/`session_send` and thus published vs left UNKNOWN) is enumerated in data-model E-4. No open mapping decision remains for Gate A beyond the blessed send-reentrancy-class question (D-7).

## Project Structure

### Documentation (this feature)

```text
specs/050-c-abi-session-send-recv/
├── plan.md              # This file
├── spec.md              # /speckit-specify + /speckit-clarify (+ plan-time refinements) output
├── research.md          # Phase 0 (this command) — D-1..D-9
├── data-model.md        # Phase 1 (this command) — E-1..E-7
├── quickstart.md        # Phase 1 (this command) — pure-C round-trip walkthrough
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
├── lifecycle_test.cpp   # NEW — create→open→start→is_established→close→destroy; register-after-start rejected; double-destroy idempotent
├── send_recv_test.cpp   # NEW — pure-C-style round-trip over loopback (send bytes; callback fires on-strand; inbound handle invalid after return — ASan negative)
├── error_block_test.cpp # NEW — session_*/app_* reachable variants → published codes; downgrade live (minor-3 code → UNKNOWN for minor-2 consumer)
├── thunk_split_test.cpp # NEW — synthetic-throw: construction (create/open/start)→*_CONFIG no abort; steady-state (send)→abort (SIGABRT trap)
└── CMakeLists.txt       # EDIT — register the new targets
```

**Structure Decision**: Single C-ABI layer — `include/fix/c_api/` public headers + `src/capi/` `extern "C"` TUs, mirroring Feature A. Split into `engine.{h,cpp}` / `session.{h,cpp}` / `config.cpp` by lifecycle owner; the internal `Application` trampoline + event-loop owner live in `engine.cpp` (engine-scoped). No new top-level module.

## Complexity Tracking

> No Constitution Check violations — table not required.

The one structural novelty (the C-ABI owning an internal io_context + worker thread, absent from Feature A) is **not** a constitution violation but is the load-bearing risk; it is isolated to `engine.cpp` and justified by the verified `engine.hpp:222` "owns NO worker threads" contract — a C consumer has no asio executor to supply, so the boundary must own one. Recorded in research D-2.
