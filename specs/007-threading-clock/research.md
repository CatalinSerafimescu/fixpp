# Phase 0 — Research & Decisions — 007-threading-clock

**Anchor:** `.specify/2d-threading.md` **v0.4** (Gate-A-converged, round 3, post-cap line-edit pass; Appendix C cross-doc entries from `2f` v1.5 / `2e` v0.4 sign-offs included). On conflict the anchor wins. All decisions below distill fixed design choices from the design doc or resolve **codebase-reality scoping** (the design doc assumes a `Session` type, a `fixpp::otel::trace_context`, and a FIX-TC corpus that do not yet exist because `005` is deferred); no design choice is invented.

---

### D-1 — `fixpp::otel::trace_context` minimal value type defined here (2k extends)

**Decision:** Define `fixpp::otel::trace_context` as a minimal 32-byte trivially-copyable, standard-layout POD — `std::array<std::byte,16> trace_id` + `std::array<std::byte,8> span_id` + `std::uint8_t flags` + `std::array<std::byte,7> _pad` (`[2d §1.2]`/§4.4). The 32-byte size, trivial-copyability, and standard-layout are **pinned by a `static_assert`** in `contracts/trace_context.hpp` (not a prose comment) — they are the contract the `std::atomic<trace_context>` snapshot rests on (the `is_always_lock_free` probe + seqlock memcpy fallback). It is the value carried by `session_local<trace_context>` and the `EngineConfig::engine_trace_context` atomic snapshot.

**Rationale:** `2k` owns the full OTel surface (TracerProvider/MeterProvider/exporters) but is not built; 2d *requires* the value type now (`current_trace_context`, the engine snapshot, every log record's `trace_id`/`span_id`). The design doc fixes the size (32 B) and lock-free-atomic intent. `2k` extends the namespace; it does **not** redefine the POD (one-direction dependency, consistent with the `[arch §2.3]` layering).

**Anchor:** `[2d §1.2]` / `[2d §4.4]` / `[2d §4.6]` / `[const §XIII.3]`.

---

### D-2 — `core::error` is the single home for the 9 threading variants; `fixpp::session::error` is a same-enum view

**Decision:** Append all **9** `[2d §6.7]` variants to the existing `include/fixpp/core/error.hpp` enum at slots **47–55** (non-renumbering, `[const §X.4]`). The design doc's `fixpp::core::error` / `fixpp::session::error` dual-naming is satisfied by the existing project convention (one `core::error` enum; "session" variants are the lifecycle subset) — **no separate `fixpp::session::error` enum is introduced** (none exists; the project keeps a single `core::error`). The lifecycle subset (`session_already_open`, `session_already_closed`) is documented as the "session" group for the C-ABI coalescing only.

**Rationale:** Minimal, matches the 001–006 precedent (one `core::error`, per-doc-prefix C-ABI coalescing). A second enum would fork the slot space and break the single-source `error.hpp` occupancy invariant. The core/session split that matters is the **C-ABI coalescing group**, not the C++ enum home (final coalescing is 2i's call). Pinned at Gate A / `/tasks`.

**Anchor:** `[2d §6.7]` / `[const §X.4]`; precedent `[2f §6.5]` (sync_* 43–46).

---

### D-3 — `session_executor` is a project-owned ASIO-executor-concept wrapper class (round 3 root cause #1)

**Decision:** `fixpp::core::session_executor` is a concrete value-typed class that **satisfies the ASIO executor concept** (`asio::execution::is_executor_v` true) and holds: the resolved inner executor (an `asio::strand<asio::any_io_executor>` under `per_session_strand`, a bare `asio::any_io_executor` under `direct_executor`) plus a typed `Session*`. It publishes `Session* session_ptr() const noexcept` as a **public member function**. Recovery at `current_trace_context` is via static type recovery on the awaiter's bound executor (`co_await asio::this_coro::executor`), NOT via `asio::any_io_executor::query`.

**Rationale:** Round 3 root cause #1 / Codex C-R3-P1-1: `any_io_executor`'s supportable property set is fixed and closed (`context_t`, `blocking_t`, `outstanding_work_t`, `relationship_t`, `allocator_t<void>`); arbitrary user-defined properties are not forwarded, so both the round-1 `query(void*)` design and the round-2 typed-`session_ptr_property` design were rejected (the latter regression-equivalent to the former). A concrete wrapper class that *is* the executor type consumers bind to survives `bind_executor`/`make_strand` (ASIO machinery operates against the executor concept, never erasing the wrapper into `any_io_executor` on engine-controlled paths). Seam 21 enforces survival (compile + runtime) and documents the rejected `any_io_executor`-cast path as known-bad (negative assertion).

**Anchor:** `[2d §4.8]` / `[2d §4.6]` / `[2d §6.1]` / Appendix C round 3.

---

### D-4 — `Session` shell scope: minimal real skeleton in `fixpp::session`, no FIX FSM (Clarifications 2026-05-19)

**Decision:** Ship a **minimal real `fixpp::session::Session`** (`include/fixpp/session/session.hpp`, optional out-of-line `src/session/session.cpp`) exposing **only** the 2d-owned surface: two-phase `close(close_mode) → asio::awaitable<expected_t<void>>`, the engine-internal `session_arena()` accessor (`[2d §4.5]` / `[2f App D §D.1]`), the `session_local<trace_context> trace_slot_` member, executor→`session_executor` binding at open, and the callback-dispatch hooks the strand serialises. **No FIX FSM logic** (Logon/gap-fill/ResendRequest/sequence-reset/concrete heartbeat values). `005` extends this same type.

**Rationale:** No `class Session` exists (verified) and the FSM is the deferred `005`. The 2d-owned surface is defined *on* `Session`; the merged `006` `async_lock_via_session_executor` declaration and the upcoming `2e` need a real `Session` bind target; a test-only fake would diverge from what `005` extends and break the traceability/completeness chain (clarify Q1 → "Minimal real skeleton"). `Session` ctor pre-conditions a non-null `session_arena` via the `[2d §4.5]` resolution chain so the accessor's never-null contract holds for the session lifetime.

**Anchor:** `spec.md` Clarifications 2026-05-19 Q1; `[2d §4.5]` / `[2d §4.6]` / `[2d §4.7]`; `[arch §2.3]`.

---

### D-5 — FSM-dependent seams via a deterministic scripted test-double FSM; seam 11 = 2d-scoped clock-injection corpus (Clarifications 2026-05-19)

**Decision:** Seams that exercise FSM-shaped behavior (3 executor-compat sequences, 9 heartbeat-window, 15 third-party `Clock` conformance, 16 `direct_executor` reentrancy) are driven by a **deterministic scripted test-double FSM** in `tests/support/` consumed by the 2d fixture; they assert only **2d-owned properties** (strand serialisation, `mock_clock` determinism, two-phase cancellation, alloc/latency), never FIX FSM correctness. Seam 15 (`tests/core/test_third_party_clock_conformance.cpp`, `[2d §9.15]`/`[2d §4.1.1]`, `2d-threading.md:1321`) drives its "Logon→NewOrderSingle→cancel" session — the on-disk realization of SC-006 — through the **same scripted test-double FSM** (a third-party `Clock` derivative injected; the FIX message labels are test-double script labels, not real FSM output), asserting only the 2d-owned properties: `sleep_until` completion on the awaiter's bound executor, root-`total` + child-state cancellation honoured, alloc-guard clean — **not** a real FIX session FSM (`005`-owned per `2d-threading.md:38`). Seam 11 is realized as a **2d-scoped deterministic clock-injection corpus** at `tests/session/test_clock_injection_corpus.cpp` (relocated from the design-doc nominal `tests/conformance/test_corpus_mock_clock.cpp`); the full FIX-TC corpus `tests/conformance/` (TC-001..017) lands with `005`. The test-double FSM picks values 2d does not pin (e.g. heartbeat interval).

**Rationale:** Clarify Q2 → "Scripted test-double FSM". The design doc references a corpus/FSM that don't exist (`005` deferred, blocked on 2d). The bounded "clock seam only" claim matches the design doc's C-P2-6 scoping; 2d claims **no FIX-TC discharge**, so the feature-completeness audit passes without a waiver. `tests/conformance/` is reserved for `005`'s FIX-TC corpus to avoid a name collision.

**Anchor:** `spec.md` Clarifications 2026-05-19 Q2; `[2d §9 seams 3/9/11/16]` / `[2d §7.9]`; C-P2-6.

---

### D-6 — Dispatch hot path: HALO-first, per-awaiter PMR fallback, `cancellable_dispatch` node from session PMR

**Decision:** The parse→`fromApp` chain is one strand-local invocation chain HALO targets for elision; where HALO does not fire, a per-awaiter PMR override constructs the promise on `SessionConfig::message_arena`. The `cancellable_dispatch` dispatch node is allocated from the awaiter's **session PMR resource** (`session_arena`), never the global heap. The `mallocnesia` guard catches **global-heap** `new`/`delete`/`malloc` only (N-P2-4 — PMR-arena allocations expected, not flagged).

**Rationale:** `[const §VIII.5]`/`[const §XI.6]` zero-global-alloc parse→`fromApp`; the design doc's §6.2/§6.5 bind the allocation budget; N-P2-4 fixes the guard semantics so the seam is not a false RED on expected PMR activity (memory `feedback_coverage_profraw_staleness` analogue for alloc guards — verify global-heap only).

**Anchor:** `[2d §6.2]` / `[2d §6.5]` / `[const §VIII.5]` / `[const §XI.6]`; N-P2-4.

---

### D-7 — Error-slot occupancy verified on this branch; 9 variants at slots 47–55, non-renumbering

**Decision:** `error.hpp` occupancy on `007-threading-clock`: slots 1, 10–13, 20–29, 30–42 (001–004); **43–46 = merged `006` `sync_*`**; **first free = 47**. The 9 `[2d §6.7]` variants are appended at **47–55** in design-doc table order:

| Slot | Variant | C-ABI coalescing group |
|---|---|---|
| 47 | `executor_already_stopped` | `FIXPP_ERR_THREAD_CONFIG` |
| 48 | `executor_not_serialised` | `FIXPP_ERR_THREAD_CONFIG` |
| 49 | `clock_sleeps_cancelled` | `FIXPP_ERR_CANCELLED` (reused, `[const §XI.2]`) |
| 50 | `strand_dispatch_failed_oom` | `FIXPP_ERR_THREAD_RUNTIME` |
| 51 | `session_already_open` | `FIXPP_ERR_THREAD_SESSION_LIFECYCLE` |
| 52 | `session_already_closed` | `FIXPP_ERR_THREAD_SESSION_LIFECYCLE` |
| 53 | `invalid_session_config` | `FIXPP_ERR_THREAD_CONFIG` |
| 54 | `clock_not_set` | `FIXPP_ERR_THREAD_CONFIG` |
| 55 | `dispatch_aborted` | `FIXPP_ERR_CANCELLED` (reused) |

The three dropped-in-design variants (`trace_context_provider_threw` per C-P2-4; `cancellation_propagation_timeout` per N-P2-1; `version_registry_dictionary_missing` per Opus N2-P2-1) are **NOT** introduced — the registry-miss path routes through the existing `[2c §6.7] dict_no_dictionary_for_application_version` (slot 28). Final C-ABI coalescing is 2i's call; 2d documents the grouping only.

**Anchor:** `[2d §6.7]` / `[const §X.4]`; verified against `include/fixpp/core/error.hpp` on this branch.

---

### D-8 — `system_clock_source` per-session reusable `steady_timer` slot pool, keyed by `Session*`

**Decision:** `system_clock_source::sleep_until` uses a per-session reusable `steady_timer` slot allocated **once** at first `sleep_until` on the session, from `SessionConfig::session_arena`, **keyed by `Session*`** (not strand handle — round 2 root cause #1, so both `per_session_strand` and `direct_executor` converge on one lifetime contract). Subsequent cycles reset `expires_at` + `async_wait` with **no allocation**. `cancel_sleeps()` walks an intrusive in-flight-awaiter list (O(N); v1.0 worst case O(2×sessions)) and signals each awaiter's slot.

**Rationale:** `[const §VIII.5]` extends to the between-message heartbeat path (N-P1-1); keying by `Session*` (round 2 root cause #1) drops the v0.2 strand-handle keying that broke under `direct_executor`. Seam 18 verifies zero global-heap after cycle 1 under both modes.

**Anchor:** `[2d §4.2]` / `[2d §6.6]` / `[2d §8]` / root cause #5 / N-P1-1 / round 2 root cause #1.

---

### D-9 — Two-phase close: child `cancellation_state` below the root; phase-1 `FileStore::flush_for_session_close()` hook; close-timeout owned by `005` (not a `SessionConfig` field)

**Decision:** `Session::close(graceful)` opens a **child** `asio::cancellation_state` composed below the session root. After the FSM's last in-flight `store(...)` awaitable has resumed and **before** the Logout `async_write` is issued, the engine invokes the engine-internal `FileStore::flush_for_session_close()` hook exactly once (non-virtual on the **concrete** `FileStore`, NOT on `MessageStore`'s pure-virtual interface — `[2e §4.1.1]`; reached via the session's `unique_ptr<MessageStore>` friend mechanism; idempotent; drains pending `commit_batched`/`commit_interval` records to durable storage; on a mid-flush `fdatasync`/`FlushFileBuffers` error completes with `expected_t::unexpected{store_io_failure}` which the engine logs before proceeding). The Logout `async_write` + its `Clock::sleep_until` timeout then bind to the **child** slot (NOT pre-cancelled by the eventual root total). Phase 2 fires `cancellation_type::total` on the root only after phase 1 resolves (peer ACK | child timeout | child cancelled). `close(terminal)` skips phase 1 entirely — the `flush_for_session_close()` hook is **not** invoked. `partial` is excluded from the v1.0 `close_mode` enum (N-P1-3). The concrete `FileStore` is owned by `2e`; 007 wires only the phase-1 call site and the seam asserts the 2d-owned ordering property only (D-5 scripted-test-double scoping). `close_timeout` is **not** a `SessionConfig` field; the value is owned by the **session-module Phase-4 spec (`005`)** and lives in *that* spec, not in 2d's frozen config shape (`[2d §4.7]`:864 / `[2d §6.7]` N-P2-1 / `[2d §6.7]`:1207 — *"the close-timeout knob lives in the session-module Phase-4 spec, not here"*). 2d wires only the *mechanism* — `deadline = effective_clock.steady_now() + close_timeout` bound to the child cancellation state (`[2d §4.7]`:800-802) — and consumes the value mechanically when `005` supplies it; 2d does NOT pick it (C-P2-8 + N-P2-1).

**Rationale:** Root cause #1 close — child-state composition makes the Logout exchange survivable independent of the eventual root total; `partial` had no well-defined per-component semantics across the {transport r/w, heartbeat, async_mutex, fromApp dispatch, store write, Logout} matrix and 2d declines to ship an underspecified parameter. The phase-1 `FileStore::flush_for_session_close()` row + hook contract were added by the **`2e` v0.4 sign-off cross-doc amendment** (`[2d §4.7]`:853,857; provenance `[2d]`:1594-1602) which this bundle's authority anchor explicitly inherits; it closes the §4.7 under-specification gap (N-P1-3) for store durability on graceful close. The earlier (incorrect) "`close_timeout` is a `SessionConfig` `std::optional<...>` placeholder owned by `005`" framing is dropped: `[2d §4.7]`:864 / `[2d §6.7]`:1207 state the close-timeout value lives in the session-module Phase-4 spec **and is not a `SessionConfig` field** — and the bundle's own `contracts/session_config.hpp` correctly carries no `close_timeout` field (only `heartbeat_interval`/`test_request_threshold`/`sending_time_threshold` are `std::optional` placeholders per `[2d §4.5]`:580-582). Seams 4/5/12 exercise the matrix.

**Anchor:** `[2d §4.7]` (as amended at `2e` v0.4 sign-off — `[2d]`:1594-1602) / `[2d §6.5]` / `[2e §7.6]` / `[2e §4.1.1]` / `[2d §4.7]`:864 / `[2d §6.7]`:1207 / root cause #1 / N-P1-3 / N-P2-1.

---

### D-10 — `mock_clock` pimpl over an opaque mutable-state object; deterministic `advance`

**Decision:** `fixpp::core::mock_clock` (public test header `<fixpp/core/test/mock_clock.hpp>`) is pimpl'd over an opaque mutable-state object (out-of-line `src/core/test/mock_clock.cpp`). `advance(delta)` walks a per-deadline ordered map and wakes every awaiter with `deadline ≤ new_steady_now`, deterministically across runs. Two identically-seeded instances driven by the same `advance` sequence produce identical `now`/`steady_now`/wake-up order (seam 1).

**Rationale:** `[const §XI.3]` bans `std::mutex` in any header that includes `asio::awaitable<...>`; `mock_clock`'s internal sync must be hidden behind the pimpl (same discipline as the `2f` `mock`-style guidance and NFR-015 §11 drop-in language "pimpl'd over an opaque mutable-state object"). Determinism is a hard test-infra property (the conformance/clock-injection corpus diff depends on it).

**Anchor:** `[2d §4.3]` / `[2d §6.6]` / `[2d §11]` NFR-015 drop-in / `[const §XI.3]`.

---

### D-11 — `[const §VII.5]`/`[const §VII.6]`/`[const §VII.7]`/`[const §IX.5]` applicability

**Decision (recorded once, mirrored in plan Constitution Check + quickstart):**
- **`[const §VII.5]` N/A-with-reason** — no `[FIX-TC]` scope; the FIX session-layer test cases need the FSM (`005`). 2d ships its own 2d-scoped clock-injection corpus (seam 11). NOT a waiver, NOT a deferred obligation (no FIX-TC discharge claimed → completeness audit passes without a waiver).
- **`[const §VII.6]` N/A** — interop needs the FSM (`005`).
- **`[const §VII.7]` strictly N/A** — 2d is not parser-touching — but a libFuzzer cancellation-timing harness (seam 12) is shipped **voluntarily** per `[2d §9 seam 12]` Gate-A discretion (`[const §VII.7]` `constitution.md:93` "Fuzzing (parser-touching modules)" voluntarily extended to threading-touching code — **not** a `[const §VII.7]`-required obligation; Article IX §4 `constitution.md:121-126` is static analysis and does **not** govern fuzzing).
- **`[const §IX.5]` N/A** — no C-ABI surface added (delegated to 2i; `[2d §5]`/§7.7/§10 Q2). 9 new `core::error` variants are C++-internal/pre-publication.

**Anchor:** `[2d Appendix B]` / C-P2-6 / `[2d §9 seam 12]` / `[2d §5]`; constitution `:91`/`:92`/`:93`/`:126`.

---

### D-12 — Appendix D cross-doc edits already landed at 2d v0.4 sign-off; only the catalogue Status-field promotion remains for the orchestrator at this feature's Gate-B merge

**Decision:** The cross-doc text amendments OWED at 2d sign-off were applied at **`2d` v0.4 sign-off (2026-05-08)** — they are **already present in the authority set**, NOT pending future work (verified 2026-05-19 against the live authority files):
- `[arch §5.4]` Trace-context Storage-bullet rename (the v0.2 `strand_local`→`session_local` finalisation; `[2d Appendix D §D.1]`) — **already present**: `architecture.md:395` reads `fixpp::core::session_local<trace_context>`.
- `[2d §11]` NFR-015 row → `library/spec/feature-catalogue.md` + a `coverage-index.md` entry linking `[2d §4.1]`/`[arch §1.1]` → NFR-015 — **already present**: `spec/feature-catalogue.md:227` carries the NFR-015 row; `spec/coverage-index.md:460` carries the NFR-015-supplemental entry (2d v0.4 sign-off 2026-05-08).
- `[arch §11]` row-7 disposition `TODO → DONE` — **already DONE**: `architecture.md:602` reads "DONE — added in `feature-catalogue.md` by 2d v0.4 sign-off (2026-05-08); coverage-index entry links `[2d §4.1]` and `[arch §1.1]` to NFR-015."
- 2k record-schema `clock_scope` drop-in — consumed by 2k at *its* sign-off (genuinely downstream; 2d records only the producer-side commitment).

The 2f-requested `[2d App D §D.1/§D.2/§D.3]` edits (the `Session::session_arena()` accessor etc.) are **already in the design-doc body** (Appendix C cross-doc entries) and are therefore **realized as the shipped 2d surface** by this feature — they are not a separate text-edit task. **Genuine residual post-merge bookkeeping (NOT done):** the catalogue **Status field** at `feature-catalogue.md:227` is still `backlog` with `PR / Tests / Verified = —`; flipping it to `done` + the PR/Tests linkage is genuine post-Gate-B-merge orchestrator bookkeeping applied at *this feature's* Gate-B merge (the row *text* was added at design sign-off; the *status promotion* is the only remaining orchestrator step — `[2c App D]` precedent).

**Anchor:** `[2d §11]` / `[2d Appendix D]` / `[2d Appendix C]` cross-doc entries; `[2c App D]` precedent; mirrors `006` research D-12.

---

### D-13 — `dict::version_registry` consumed via the merged `[2c §4.9]` API; FIXT.1.1 miss routes to `[2c §6.7]`

**Decision:** `EngineConfig::dictionaries` → the engine builds `dict::version_registry` at `Engine::open` via the merged-`003` `[2c §4.9]` API (`get(application_version) -> Dictionary const*`, borrowed pointers, `shared_ptr<const Dictionary>` keepalive). A FIXT.1.1 per-message `ApplVerID(1128)` miss surfaces as the existing `[2c §6.7] dict_no_dictionary_for_application_version` (slot 28 → `FIXPP_ERR_DICT_CONFIG`), **not** a 2d-layer synonym. Seam 20 enforces both the dispatch-time and the `Engine::open` registry-construction paths route through the 2c variant.

**Rationale:** Opus N2-P2-1 — a 2d-layer synonym would route the same failure to a different C-ABI group; the registry is built through the 2c API which raises the 2c variant directly. 003 is merged so the API is available.

**Anchor:** `[2d §4.4]` / `[2d §6.7]` / `[2c §4.9]` / `[2c §6.3]` / `[2c §6.7]`; Opus N2-P2-1.

---

### D-14 — Consumed-not-built upstream surfaces (006/003/004 merged)

**Decision:** This feature **consumes, does not build**: the merged `006` `async_mutex` executor-compat surface (`[2f §7.4]`/`[2f §4.1.1]`) — 2d ships the real `session_executor`/`Session::session_arena()` backing the already-merged **declaration-only** `include/fixpp/session/async_lock_via_session_executor.hpp` (006 RC#2 layering boundary); the merged `003` `dict::version_registry`/`reify` (`[2c §4.8]`/`[2c §4.9]`); the merged `001`/`002`/`004` `core`/`wire` baseline (`expected_t`, `error`, `detail::trap_throw`, `MessageView`, three-arena PMR). No upstream code is modified; the only additive edit to a merged file is the non-renumbering `error.hpp` slot append (D-7).

**Rationale:** Faithful to the `2f → 2d → 2e` prerequisite ordering (CLAUDE.md / `spec.md` Assumptions). 006's session-side helper was deliberately declaration-only (impl owned by the session-module spec); 2d ships the `session_executor`/`session_arena()` it binds against, closing the layering loop without re-litigating 006.

**Anchor:** `[2f §4.3.2]` / `[2f App D §D.1]` / `[2c §4.9]` / `[arch §2.3]`; CLAUDE.md prerequisite ordering; verified `include/fixpp/session/async_lock_via_session_executor.hpp` present on this branch.
