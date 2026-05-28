# Implementation Plan — 013-session-reconnect-binding

**Branch**: `013-session-reconnect-binding` | **Date**: 2026-05-28 | **Spec**: [spec.md](spec.md)

**Design anchors**: this slice has **NO Phase-2 design doc of its own** — the session-module design surface is split across `architecture.md §4.4` v0.2 (session-module surface inventory + `[arch §5.6]` frozen-at-open carve-out for the mid-session-mutable shapes this feature lands), `005-session-establishment-fsm` (the 6-state `fsm_state` machine + `seqnum_manager` ground truth — `[2e-FSM]`-equivalent), `010-session-cfg-lifetime` (the `SessionConfig` value type + the `SessionEvent` ring-buffer accessor F-04 closure), and the upstream design docs `.specify/2g-tls.md` v0.4 (`verify_peer` + `peer_identity` + `tls_verify_error` 15-variant surface — consumed for FR-019 / FR-026), `.specify/2h-transport.md` v0.3 (`Transport` + `TlsTransport` + `ReconnectPolicy` + `TransportFactory::make` + `handshake_result.peer_id` — consumed for FR-001 / FR-002 / FR-019 / FR-033), `.specify/2j-controlplane.md` v0.2 (control-plane reload-trigger reservation `[2j §3.12]` — IN-PROCESS variant lives here, gRPC variant deferred per 2j RC#5 to v1.x), `.specify/2d-threading.md` v0.4 (`session_executor` + `cancellable_dispatch` + per-mode cancellation effect table — heartbeat/test-request cadence consumes), `.specify/2e-msgstore.md` v0.4 (`MessageStore::retrieve` + `PossDupFlag(43)=Y` re-emit semantics — consumed for FR-010 outbound replay + FR-015 dedup), `.specify/2a-decimal.md` v0.3 (`trap_throw` PMR-boundary discipline), the `constitution.md` v0.2 (Article XI Concurrency & Coroutines; Article XII Security & TLS; Article XV banned patterns; Article XVII Codex review gates), and the `.specify/architecture.md` v0.2 sibling. On conflict with any of the cited anchors, the cited anchor wins; a divergence is a plan defect. **First-of-its-kind anchor pattern**: 013 is the first feature anchored by 005's *spec/plan* rather than a dedicated `.specify/2N-*.md` design doc — the session-module-Phase-4 surface was deliberately specified incrementally (005 → 009 → 010 → THIS) rather than authored as a single design doc up front. Re-emission discipline still applies (FR/SC text quotes the upstream verbatim where appropriate); the "design doc wins on conflict" rule is satisfied by treating 005's shipped headers + 010's `SessionEvent` accessor + 012's `Transport`/`TransportFactory` shapes as the binding contracts.

**Pipeline state**: `/speckit-specify` (2026-05-28) → `/speckit-clarify` (5 Qs resolved 2026-05-28 with reference-engine sweep across QuickFIX-cpp v1.16.0 / QuickFIX/J v3.0.1 / Fix8 v1.4.3 per `[[feedback_always_invoke_speckit_clarify]]`) → **`/speckit-plan` (this doc)** → next per `[const §XVII.1]` / `[pipeline.md step 4]` is **Phase-4 Gate A** on the 013 bundle. Then `/speckit-tasks` → `/speckit-analyze` → `/speckit-checklist` → `/speckit-checklist-audit` (MANDATORY gate, executor = checklist-auditor agent per `[pipeline.md [I]]`) → `/speckit-implement` → `/simplify` → `/speckit-verify` → Gate B.

## Summary

Ship the **session-Phase-4 surface** — the reconnect FSM driver + recovery sub-protocol + CompID↔TLS-identity binding policy + TLS-validation-outcome plumbing into `SessionEvent` + in-process credential rotation. Closes four cross-feature obligations in one bundle:

1. **Recovery upgrade** — amend 005 FR-008 from "fatal-disconnect on any inbound sequence gap" to **recovery-active**: issue `ResendRequest(2)` for missing inbound range, replay outbound from `MessageStore` with `PossDupFlag(43)=Y` + `OrigSendingTime(122)` on inbound `ResendRequest`, collapse all-admin spans to `SequenceReset-GapFill(4)`, honour `ResetSeqNumFlag(141)=Y` per operator-configured policy. Catalogue rows S-005 / S-006 / S-014-FSM-half / S-024 flip to `done`. 44 cross-comms scenarios currently tagged `fixpp gap` (per `[[project_2e_recovery_v1_upgrade_obligation]]`) become executable; 4 in-code `2e-recovery` markers removed.
2. **T-041 — CompID↔TLS-identity binding** (FIXS §4.4) — bind the `peer_identity` (from 011's `verify_peer` outcome via 012's `handshake_result.peer_id`) to the asserted `SenderCompID(49)` / `TargetCompID(56)` at Logon-time using an operator-supplied `CompIdAuthorizationPolicy` value type; default-deny allow-list per Clarifications Q3=A; canonical-fixed principal extraction `CN → SAN-DNS → SAN-URI → SHA-256-fingerprint` per Clarifications Q2=A; symmetric coverage initiator AND acceptor per `[[feedback_half_restructure_symmetric_api]]`.
3. **T-039 second half — TLS validation outcome → `SessionEvent`** — surface every `tls_verify_error` variant from 011's `[2g §6.6]:986-1004` (15 variants) as a distinct `SessionEvent::tls_validation_failed{variant, peer_endpoint, reason_string}` on the existing 010-F-04 ring-buffer; the event channel is bound to the `Listener` / `Session` config (no Session instance need exist), so handshake-fail-before-Logon still surfaces.
4. **T-040 second half — in-process `reload_credentials` control plane** — `Session::reload_credentials(new_cert_source)` + `asio_listener::reload_credentials(new_cert_source)` atomically swap the underlying `cert_source` at `transport_factory::make(...)` entry per Clarifications Q4=A; in-flight handshake observes the OLD source; the NEXT handshake observes the NEW. Worst-case rotation latency = one handshake (~50–500 ms). **Constitutional alignment**: 2j-controlplane's IN-PROCESS variant `[2j §3.12]` lands here; the gRPC trigger remains deferred to v1.x per 2j RC#5 (no new C-ABI symbol; no new gRPC RPC).

New types under `include/fixpp/session/`:

1. **`fixpp::session::ReconnectFsm`** (new) — driver layer on top of the shipped 6-state `fsm_state` (NotConnected → LogonSent / LogonReceived → Active → LogoutSent → Disconnected); owns the `ReconnectPolicy` walk, per-attempt `Transport` minting via the shipped `TransportFactory`, the recovery sub-state `AwaitingResend` (a transient flag on `Active`, NOT a new `fsm_state` value — preserves the 6-state enum's ABI per `[arch §5.6]` frozen rule), the Heartbeat / TestRequest cadence timers, and the Logout / force-disconnect timeout.
2. **`fixpp::session::ResendState`** (new) — per-session state for the Resend sub-protocol: tracks outstanding `ResendRequest` we issued (`BeginSeqNo`, `EndSeqNo`, `started_at`), inbound replay we are receiving, outbound replay we are sending; lives across `AwaitingResend`; reset on protocol exit.
3. **`fixpp::session::CompIdAuthorizationPolicy`** (new) — value type carrying allow-list bindings `{principal → {compid_set}}`; `authorize(peer_identity const&, std::string_view asserted_compid) noexcept -> expected_t<bound_principal>`. Allow-list only in v1.0 per FR-023 / Clarifications Q3=A; empty policy = default-deny (rejects all Logons). Constructed at `SessionConfig`-build time; consulted per Logon. Principal extraction in canonical-fixed order per FR-022 / Clarifications Q2=A.
4. **`SessionConfig` extensions** — three new fields per the `[arch §5.6]` frozen-at-open carve-out: `reset_seqnum_policy: enum { bilateral_strict, bilateral_lenient, unilateral }` (default `bilateral_strict` per Clarifications Q1=A); `logout_disconnect_timeout_ms: std::uint32_t = 2000` (per Clarifications Q5=A; QuickFIX/J `SessionState.logoutTimeoutMs=2000L` precedent); `compid_authorization_policy: CompIdAuthorizationPolicy` (default-constructed = empty allow-list = default-deny).
5. **`SessionEvent` variant extensions** — 5 new variants on the existing 010-F-04 ring-buffer accessor (NO new event channel, NO breaking change to existing variants): `peer_identity_bound{cn, sans, sha256_fingerprint, cipher, bound_compid, principal_source}`, `compid_authorization_failed{cn, asserted_compid, expected_compids[]}`, `tls_validation_failed{variant: tls_verify_error, peer_endpoint, reason_string}`, `credentials_rotated{old_sha256, new_sha256}`, `sequence_numbers_reset{by_peer_request: bool}`. The `tls_validation_failed::variant` is the precise `[2g §6.6]:986-1004` enum value, NOT a coalesced "tls error" per FR-026.
6. **`Session::reload_credentials` + `asio_listener::reload_credentials`** — engine-surface methods (NOT pure-virtual on any abstract `Listener` base per `[[feedback_half_restructure_symmetric_api]]` lesson + 012's `Listener::cancel` precedent: pluggable interface ≤5 pure-virtual cap is at `Listener`'s 1-of-5 already; reload is a concrete-impl concern); atomically swap the underlying `cert_source` on the held `SessionConfig` / `TransportFactory`; in-flight Active sessions unaffected; next reconnect handshake picks up rotated cert.
7. **`Session::logout(std::chrono::milliseconds timeout)`** — initiator-graceful logout entry per FR-008; emits `Logout(5)`, awaits peer reply for `SessionConfig::logout_disconnect_timeout_ms` (default 2000 ms), closes Transport, surfaces `error::session_logout_disconnect_timeout` if elapsed before reply.

Plus the error envelope:

8. **5 new `error::session_*` variants** at the next free contiguous block 116..120 immediately after 012's `transport_*` block (which occupies 94..115; 22 slots per shipped post-PR-#85 header). NO C-ABI grouping change; coalesces under the existing `FIXPP_ERR_SESSION` group owned by 2i. Per Assumption A.8: slot allocation goes through the same `[const §X.2]` ABI-hygiene gate as prior error variants; the `/speckit-implement`-time rewriter MUST cross-check the actual `include/fixpp/core/error.hpp` to confirm the boundary remains at 115 (NO ±N drift from the 012 close-out); never renumber existing slots.
   - `session_seqnum_reset_mismatch = 116` — FR-017 `bilateral_strict` mode reject
   - `session_compid_unauthorized = 117` — FR-021 binding-policy reject
   - `session_logout_disconnect_timeout = 118` — FR-008 Logout-reply window elapsed
   - `session_heartbeat_timeout = 119` — FR-004 inbound liveness window elapsed
   - `session_testreqid_mismatch = 120` — FR-006 inbound Heartbeat carries TestReqID that doesn't match the most recent outbound TestRequest

**Catalogue rows owned / co-owned**:
- **Flips to `done` on merge**: S-005 (ResendRequest issue), S-006 (ResendRequest reply), S-014-FSM-half (recovery FSM transitions), S-024 (SequenceReset GapFill), T-039 (TLS validation outcome — full row; 011 owned the predicate, 012 owned the trampoline wiring, 013 owns the session-event surfacing), T-040 (cert-source rotation — full row; 010/011 owned the storage, 013 owns the control-plane entry-point), T-041 (CompID↔TLS-identity binding — full row).
- **Cross-cuts amended in-place**: 005 FR-008 (fatal-disconnect → recovery-active); 4 in-code `2e-recovery` upgrade markers removed; 44 `fixpp gap` cross-comms scenario tags dropped per `[[project_2e_recovery_v1_upgrade_obligation]]`.

**Branch base**: `013-session-reconnect-binding` rooted on post-PR-#85-merge `main` of the library submodule (`53e25b1` → parent `8fdc12c`). **Inbound binding contracts** (all merged, A.1 dependency-precondition verified): 005-session-establishment-fsm + 009-session-fsm-finalize (PR #82 squash `ba2222d`), 010-session-cfg-lifetime (PR #83 squash `25cf09d`), 011-tls-policy (PR #84 squash `67448c0`), 012-2h-transport (PR #85 squash `53e25b1`). NO carry-overs to discharge — 011 F-1 was discharged in 012 via `tests/transport/test_load_credentials_seam13_witness.cpp`; 012's 3 Gate B carry-forward waivers (RC#C PMR-OOM witness depth + RC#G handshake bench live fixture + RC#I fuzz scope catalogue re-label) are a separate post-012 slice off `main`, NOT this feature's scope. Codecov DA/BRDA carry-forwards (W-2 cppcheck + W-3 iwyu inherited from 011 + W-format / W-tidy carry from prior PRs) remain applicable per `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]`.

**Gate A required**: yes. This feature opens the session-Phase-4 surface — the FSM amendment (FR-009 amends 005 FR-008) AND the new `CompIdAuthorizationPolicy` AND `reload_credentials` cross every Appendix A trigger row (session FSM, recovery semantics, gap-fill rules, security cert-source plug-in, error semantics with 5 new variants). All four mandatory controls demanded by `[const §XVI.3]` / `[const §XVII.1]`: `/clarify` ✓ 2026-05-28, `/analyze`, Codex Gate A, user `/plan` sign-off.

## Technical Context

**Language/Version**: C++23 (`[const §II.1]`). Coroutines (`asio::awaitable<T>`), `core::expected_t`, `std::pmr`, `std::span`, `std::variant`, `std::array`, `std::chrono`, `std::unique_ptr`, `std::shared_ptr` (for the `cert_source` shared-ownership model carried forward from 010 A1 amendment + 011's `Pinset::snapshot()` capture pattern), `[[clang::lifetimebound]]` on view-returning accessors (`peer_identity::cn_view()`, `bound_principal::value_view()`), `[[nodiscard]]` on every `expected_t<T>`-returning method, deducing `this` where helpful.

**Primary Dependencies**: **No new Conan rows.** All consumed surfaces are already-shipped:
- **`fixpp::transport`** (shipped by 012 — LOCKED): `Transport`, `TlsTransport`, `TransportFactory::make(asio::any_io_executor, fixpp::tls::SslCtxConfig, std::pmr::memory_resource*) noexcept -> expected_t<std::unique_ptr<Transport>>` per `[2h §4.7]:907-910`, `ReconnectPolicy` (schedule-array + jitter + max_attempts per `[2h §4.4]`), `handshake_result.peer_id`, `asio_listener::async_accept` + `asio_listener::cancel`. The factory's cached-SSL-CTX FR-026 contract from 012 is BINDING for this feature's `reload_credentials` invariant — the cache invalidation point is the atomic swap at `transport_factory::make(...)` entry (see Clarifications Q4=A; this is the surface 2j-controlplane's IN-PROCESS variant `[2j §3.12]` reserves).
- **`fixpp::tls`** (shipped by 011 — LOCKED): `cert_source`, `file_cert_source`, `Pinset` + `pin_view` + `pin_snapshot`, `verify_peer(...) -> expected_t<peer_identity, tls_verify_error>`, the 15 `tls_verify_error` variants per `[2g §6.6]:986-1004`, `peer_identity` (CN / SANs / SHA-256 fingerprint / cipher suite — the binding surface for FR-019 + FR-022).
- **`fixpp::session`** (shipped by 005/009/010 — extended here): `fsm_state` 6-state enum (extended via transient `AwaitingResend` flag on `Active`, NOT a new enum value), `Session`, `SessionConfig` (extended with 3 new fields), `SeqnumManager`, `admin_messages` builders (FR-001 / FR-003 / FR-007 consume), `MessageStore` interface (FR-010 outbound replay, FR-015 dedup, FR-012 store-horizon GapFill consume), the `SessionEvent` ring-buffer accessor from 010 F-04 close-out (extended with 5 new variants).
- **`fixpp::core`**: `expected_t`, `error` (the file 012 amended with `error::transport_*` variants; 013 appends 5 `error::session_*` variants at the next free slots 116..120, never renumbering, per `[[project_2e_design_doc_only_seqnum_handoff]]`), `trap_throw` per `[2a §4.2]`, `Clock` (heartbeat cadence + Logout timeout consume via `effective_clock` per `[2d §7.9]`), `cancellable_dispatch` per `[2d §6.5]`, `session_executor` per `[2d §4.8]`.
- **ASIO + OpenSSL** — same rows 011/012 already pinned; 013 introduces no new ASIO/OpenSSL surface (consumes via 012's `Transport` abstractions).
- **GoogleTest 1.17.0** + **Google Benchmark 1.9.5** pinned (unchanged). **libFuzzer** for 1 parser-touching seam (`tests/fuzz/fuzz_session_recovery_admin_parse.cpp` — peer ResendRequest / SequenceReset / Logon-with-141=Y framing into the FSM through 2b Framer; ASan + UBSan invariants per `[const §IX.4]`).

**Storage**: N/A directly. The recovery sub-protocol consumes `MessageStore::retrieve` (from 008 — LOCKED) to replay outbound on inbound `ResendRequest`; durable-before-transmit linearisation invariant per `[2e §6.1.4]` is consumed UNCHANGED.

**Testing**: GoogleTest + Google Benchmark (C++), TDD red-green-refactor per `[const §VII.1]` / `[const §VII.3]`. Deterministic time via `fixpp::core::mock_clock` (Heartbeat / TestRequest / Logout-timeout / reconnect-delay tests). Deterministic in-memory transport via `fixpp::transport::test::mock_transport` (the public-test header 012 shipped) for FSM-under-test recovery seams — the recovery sub-protocol is exercised entirely against `mock_transport` (no OpenSSL / ASIO networking linkage); the CompID-binding + TLS-validation-event surfaces are exercised against `asio_tls_transport` over loopback (3 reference engines as cross-engine cells per `[[project_release_interop_quickfix_fix8]]`, deferred to the 014 interop-harness feature). 1 libFuzzer harness on the recovery admin-message parse path. Sanitizer matrix per `[const §IX.2]`: Clang Debug + Release + ASan + UBSan + **TSan** + Coverage; GCC Release sanity. TSan is critical because the `Heartbeat` cadence timer + the `AwaitingResend` transient sub-state + the `reload_credentials` atomic swap all touch cross-strand state.

**Target Platform**: Same Tier-1 matrix as 005–012. **No C-ABI surface added by 013** — `[const §IX.5]` abidiff N/A. The IN-PROCESS `reload_credentials` is a C++-only API; the gRPC control-plane trigger is deferred to v1.x per `[2j §3.12]` RC#5 + Article XVIII roadmap. The session-Phase-4 C-ABI bridge is a separate later feature (likely 015+) under 2i; 013 publishes the C++ source-of-truth.

**Project Type**: C++23 library, **EXTENDS the session module** — no new module directory. `include/fixpp/session/*.hpp` gains 4 new headers + extends 3 existing; `src/session/*.cpp` gains 4 new files + extends 2 existing. `tools/check_layers.py` is UNCHANGED (no new module — session/ already registered).

**Performance Goals** (per `[const §VIII.2]` ±5 % regression budget on hot-path rows; ±2× soft on cold-path rows):
- **Heartbeat cadence emit** on Active session, warm-cache: ≤ 500 ns p99 — `mock_clock::now()` (≈50 ns) + admin builder (`[const §VIII.5]` allocator policy: per-session PMR arena, no global new/delete) + `Transport::async_write` issue (≤ 200 ns p99 per `[2h §6.3]` row 2).
- **`CompIdAuthorizationPolicy::authorize(peer_identity, asserted_compid)` lookup**: ≤ 5 µs p99 (per SC-003: handshake-to-Logon-reject latency budget ≤ 5 ms p99 per `[const §VIII.2]`, so the policy lookup itself must be a sub-microsecond fraction of that). Default storage: `std::pmr::unordered_map<principal_view, compid_set>` with monotonic-arena backing; `principal_view` is a zero-copy string_view into `peer_identity` material captured at handshake time.
- **`Session::reload_credentials` operator call**: ≤ 50 µs p99 (cold-path; PMR-arena allocation of new `cert_source` material). The atomic swap itself is one `std::atomic<std::shared_ptr<cert_source>>::store(...)` at `transport_factory::make(...)` entry — O(1) under no contention.
- **Recovery throughput** (per SC-005): replay 1000-message inbound gap (mixed app + admin span) within 2× the wall-clock the peer took to send those messages, measured against the slowest of the three reference engines. The `MessageStore::retrieve` + `PossDupFlag` re-emit path is the bottleneck; no new perf surface beyond 008's already-budgeted retrieve cost.

CI fails on > 5 % regression on the hot-path rows (Heartbeat cadence; CompID authorize lookup); > 2× on the cold-path rows (`reload_credentials`).

**Constraints**:
- **Zero global `new`/`delete` on the inbound dispatch path** per `[const §VIII.5]` / `[const §XV.1]` — the FSM transition from `Active` → `AwaitingResend` and back MUST NOT allocate (the `ResendState` is a fixed-size struct embedded in the session). Verified via `tests/perf/test_session_recovery_alloc_guard.cpp` + the **dual allocation guard** (`counting_resource` PMR-routed accounting AND mallocnesia LD_PRELOAD interception of global `operator new` / `malloc`) per `[[feedback_tracking_pmr_resource_false_pass]]`.
- **PMR allocation permitted** on cold paths only: `CompIdAuthorizationPolicy` storage (operator-supplied bindings at SessionConfig-build time), `peer_identity` SAN-string copies (already 011's surface — 013 stores a snapshot via `[[clang::lifetimebound]]` view discipline), `reload_credentials` arena for new `cert_source` material. PMR throws routed through `[2a §4.2]` `trap_throw` and surface as `session_*` variants — NO PMR throw escapes as a C++ exception across the public surface.
- **Construction-time throws** permitted per `[arch §5.3]` carve-out (engine bootstrap; `SessionConfig::compid_authorization_policy` build-time validation may throw if the operator passes a malformed binding). Runtime entry-points (`reload_credentials`, `authorize`, `Session::logout`) wrap the boundary in `expected_t<...>`.
- **ASIO native cancellation slots end-to-end** on every async method per `[const §XI.2]`. Heartbeat timer + TestRequest timer + Logout timeout + reconnect-delay sleep all consume `co_await asio::this_coro::cancellation_state`; `cancellation_type::total` MUST surface as the matching `transport_*_cancelled` / `session_*_cancelled` variant.
- **Two-phase close** per `[2d §4.7]` per-mode effect table extended in `[2h §6.4.1]` MUST be honoured on every Logout / force-disconnect path.
- **Per-session strand serialisation** per `[const §XI.4]`. The `reload_credentials` atomic swap operates on the `TransportFactory`'s held `cert_source` slot via `std::atomic<std::shared_ptr<cert_source>>` — strand-free per `[2d §6.5]` cancellable_dispatch pattern; defence-in-depth via the FSM-owned strand.
- **No C-ABI surface emitted by 013** (`[const §X.2]` nm check inherited). 2i C-ABI bridges and 2j gRPC reload-trigger are post-013; 013 publishes the C++ source-of-truth + the IN-PROCESS reload variant only.
- **Pluggable interface caps**: NO new abstract interfaces. `CompIdAuthorizationPolicy` is a VALUE TYPE (not an interface — per `[const §XIV.2]` ≤5 pure-virtual cap, value types don't count); the `reload_credentials` entry-point is a concrete method on `Session` / `asio_listener` (NOT pure-virtual on any abstract base — same convention as 012's `asio_listener::cancel`).
- **No transport-internal write queue** per `[arch §5.8]` + `[const §XV.15]` — the strand IS the depth-1 queue; FR-039 (block mode only) is inherited from 012 verbatim.
- **Half-restructure discipline** per `[[feedback_half_restructure_symmetric_api]]` + Assumption A.5: every initiator / acceptor symmetric contract (`reload_credentials` initiator AND acceptor; `CompIdAuthorizationPolicy::authorize` initiator path AND acceptor path; Heartbeat / TestRequest / Logout symmetric) is implemented + tested + audited in BOTH halves. 012 RC#B's saga cost 2 extra Gate B rounds + the full Codex 2/2 attempt budget; the rule is binding here.
- **Production-shaped entry-point exercise** per `[[project_005_phase8_completeness_false_pass]]` — completeness-audit step audits test BODIES, not file names; `SUCCEED()`-placeholder tests count as MISSING coverage. The recovery sub-protocol's tests MUST drive bytes through the FSM (via `mock_transport`), NOT via direct method calls on internal helpers.

**Scale/Scope**: Estimated edit footprint (4 new headers + 4 new .cpp + 3 extended headers + 2 extended .cpp under session/):

- **Headers**: 4 new public headers under `include/fixpp/session/` — `reconnect_fsm.hpp` (ReconnectFsm + AwaitingResend transient state + Heartbeat/TestRequest/Logout-timeout machinery), `resend_state.hpp` (ResendState struct + helpers), `compid_authorization_policy.hpp` (value type + `authorize(...)` entry-point + `bound_principal` value), `session_event.hpp` (the SessionEvent variant union — extracted as a public header if 010 left it internal; the 5 new variants land here). Plus 3 EXTENDED public headers: `session.hpp` (adds `reload_credentials`, `logout(timeout)`), `session_config.hpp` (adds `reset_seqnum_policy` + `logout_disconnect_timeout_ms` + `compid_authorization_policy` fields), `session_fsm.hpp` (adds `AwaitingResend` transient flag — NOT a new `fsm_state` enum value per `[arch §5.6]` frozen-at-open rule). Public surface: ~600 lines new + ~150 lines extensions.
- **`include/fixpp/core/error.hpp`** appended with **5 new `error::session_*` enum variants** at the contiguous block 116..120 immediately after 012's `transport_*` block (which occupies 94..115 per the shipped post-PR-#85 header — `transport_accept_cancelled = 115` per `[2h §6.6]:1199`). `/speckit-implement`-time cross-check the actual `include/fixpp/core/error.hpp` to confirm the boundary remains at 115; future ±N adjustment (e.g., if a 012 carry-forward waiver-close ships an additional `transport_*` variant) gets reconciled at /implement-time without re-running Gate A. NEVER renumber existing slots.
- **Implementation**: 4 new `.cpp` files under `src/session/` — `reconnect_fsm.cpp` (Heartbeat/TestRequest cadence + AwaitingResend entry/exit + ResendRequest emit + per-attempt Transport mint), `resend_state.cpp` (out-of-line bodies for ResendState helpers — mostly header-only), `compid_authorization_policy.cpp` (authorize body + principal-extraction canonical-order helper), `session_event.cpp` (event-variant emit helpers — mostly header-only). Plus 2 EXTENDED .cpp: `session.cpp` (reload_credentials body + logout(timeout) body), `seqnum_manager.cpp` (FR-013 SequenceReset-GapFill advance without store; FR-015 PossDupFlag dedup). Total ~1.2 k–1.5 k lines new + ~250 lines extensions.
- **Tests** (the test list maps to the FRs + the 7 named seams in §Phase 1 Test Plan below): ~10 new `tests/session/test_*.cpp` files + 1 fuzz harness + the 8-cell recovery witness matrix. Total ~2.0 k lines of test code. Critical seams: recovery happy-path + admin-span-collapse + store-horizon + 141=Y three-mode matrix + binding-policy default-deny + binding-policy mismatch + tls_validation_failed all-15-variants + reload_credentials in-flight-handshake-defer + 8-cell heartbeat-cadence matrix.
- **Bench**: 2 new bench binaries — `bench/session/bench_heartbeat_cadence.cpp` (Heartbeat emit ≤ 500 ns p99) + `bench/session/bench_compid_authorize.cpp` (authorize lookup ≤ 5 µs p99). The `reload_credentials` cold-path is NOT benched (cold-path soft ceiling; one-time operator call per rotation).
- **Documentation**: `docs/src/session-recovery-quickstart.md` + `docs/src/compid-binding-quickstart.md` + the `quickstart.md` companion in this spec dir (Phase 1 output). `library/spec/feature-catalogue.md` + `library/spec/coverage-index.md` updates per `[[feedback_feature_completeness_gate]]` — append 013-session-reconnect-binding row + flip S-005/006/014-FSM-half/024 + T-039/040/041 to `done` post-merge.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked post-Phase 1.*

| Article | Clause | This feature | Status |
|---|---|---|---|
| II.1 | C++23 | C++23 throughout (coroutines, `core::expected_t`, `std::pmr`, `std::variant`, `[[clang::lifetimebound]]`, deducing `this`) | ✓ |
| VI | Spec Coverage Discipline | Catalogue rows S-005 / S-006 / S-014-FSM-half / S-024 / T-039 / T-040 / T-041 flip `done`; 4 in-code 2e-recovery markers removed; 005 FR-008 amended in-place; 44 `fixpp gap` cross-comms tags dropped | ✓ |
| VII.1 / VII.3 | TDD red-green-refactor | Tests authored first per phase per `[[feedback_subagent_phase_verification_two_traps]]` | ✓ (procedural; enforced at /tasks + /implement) |
| VII.5 | Conformance corpus passes | TC-001..TC-017 re-run against `asio_tls_transport` + this feature's FSM (no regression on already-passing cases; previously-`gap`-tagged cases flip to `pass`) | ✓ |
| VII.6 | Interop test against independent FIX impl | QuickFIX-cpp / QuickFIX/J / Fix8 interop matrix is the 014-interop-harness feature (separate Spec-Kit cycle); 013 unblocks it per `[[project_release_interop_quickfix_fix8]]` | ✓ (procedural — unblock obligation, not in-feature delivery) |
| VII.7 | Fuzz / determinism on parser-touching surfaces | `tests/fuzz/fuzz_session_recovery_admin_parse.cpp` (peer ResendRequest / SequenceReset / Logon-with-141=Y framing into FSM through 2b Framer); ASan + UBSan invariants | ✓ |
| VIII.1 / VIII.2 / VIII.3 | Perf bench gate (±5 % hot-path; ±2× cold-path) | Heartbeat cadence + CompID authorize benches; ceilings recorded above; CI fails > 5 % regression | ✓ |
| VIII.5 | Zero `new`/`delete` on hot path | FSM Active ↔ AwaitingResend transition + Heartbeat emit are zero-alloc; verified via mallocnesia + counting_resource dual gate per `[[feedback_tracking_pmr_resource_false_pass]]` | ✓ |
| IX.1 | ≥95 % line / ≥85 % branch on touched modules | Targeted via test plan; YELLOW carve-out per `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]` if recovery fault-injection paths (peer-side admin span collapse cells; partial Resend; store-horizon edge) cannot be reached without injecting OpenSSL-level fault hooks (precedent: PR #73 / #74 / #77 / #82 / #84 / #85) | ✓ (planned; documented if YELLOW) |
| IX.2 | Sanitizer Tier 1 ASan+UBSan+TSan | All three enforced per /speckit-verify | ✓ |
| IX.4 | Fuzz on parser-touching surfaces | 1 libFuzzer harness ships: recovery admin parse | ✓ |
| IX.5 | abidiff on C ABI changes | N/A — 013 emits no C ABI | ✓ |
| X.2 | No `extern "C"` symbol emitted | `nm` check inherited | ✓ |
| XI.2 | ASIO native cancellation slots | Heartbeat timer + TestRequest timer + Logout timeout + reconnect-delay sleep all honour `cancellation_type::total` end-to-end | ✓ |
| XI.3 | `fixpp::sync::async_mutex` in coroutine context | NOT directly consumed (FSM uses strand serialisation per `[const §XI.4]`; the cert-source atomic swap uses `std::atomic<std::shared_ptr<...>>`, not `async_mutex`); `[const §XV.9]` `std::mutex`-in-coroutine ban honoured | ✓ |
| XI.4 | Per-session strand | All FSM transitions on session strand; `reload_credentials` is the one strand-free entry (atomic swap; safe by construction) | ✓ |
| **XII (Security & TLS)** | TLS rules + `SecurityProfile` no-implicit-default + pluggable `cert_source` + security mandatory controls | All eight subsections honoured; the binding-policy adds defence-in-depth at the application layer above the TLS layer; `[2g §6.6]` `tls_verify_error` variants surface verbatim (FR-026) — no coalescing | ✓ (central — see Phase-0 research §1–§5) |
| XIV.2 | Pluggable interface ≤ 5 pure-virtuals | NO new abstract interfaces — value types only (`CompIdAuthorizationPolicy`, `ResendState`); `reload_credentials` is a concrete method on `Session` / `asio_listener` per the 012 `Listener::cancel` convention | ✓ |
| XIV.4 | No `dlopen` plugin loading | Compile-time selection only | ✓ |
| XV.5 | No sync logging on hot path | Heartbeat cadence + FSM transitions do NOT log; cold-path logging (CompID authorize fail, binding event, tls_validation_failed event, credentials_rotated event) routes through async logger via T-2k integration | ✓ |
| XV.9 | No `std::mutex` in coroutine context | 013 uses strand serialisation + ASIO cancellation; the `std::atomic<std::shared_ptr<cert_source>>` swap is not in coroutine context (it's the operator-call entry) | ✓ |
| XV.15 | No `drop-oldest` on app/session message paths | v1.0 ships `block` mode only (inherited from 012 verbatim); MessageStore replay path consumes `block` per 008 | ✓ |
| XVI.3 | `/clarify` mandatory for security / FSM | Completed 2026-05-28, 5 Qs resolved with reference-engine sweep (ResetSeqNumFlag policy / principal extraction / allow-list mode / reload-credentials concurrency / Logout timeout default) | ✓ |
| XVI.7 | `/simplify` before `/speckit-verify` | Procedural — runs post-/implement, pre-verify | ✓ (procedural) |
| XVII.1 | Phase-4 Gate A blocks `/tasks` | Next gate after this `/plan` | ✓ (procedural — not yet executed) |
| XVII.8 | `/speckit-verify` mandatory; paired-evidence labels | Procedural — `/speckit-verify` runs post-/simplify, pre-Gate-B | ✓ (procedural) |

**No constitution violations to track.** No `Complexity Tracking` table entries needed. The `AwaitingResend`-as-transient-flag (NOT a new `fsm_state` enum value) is the explicit design choice that preserves the 6-state enum's ABI per `[arch §5.6]` frozen-at-open rule; the 5 new error slots at the next free contiguous block 116..120 respect the [const §X.2] append-only ABI discipline.

## Project Structure

### Documentation (this feature)

```text
specs/013-session-reconnect-binding/
├── plan.md              # This file (/speckit-plan output)
├── research.md          # Phase 0 output (/speckit-plan)
├── data-model.md        # Phase 1 output (/speckit-plan)
├── quickstart.md        # Phase 1 output (/speckit-plan)
├── contracts/           # Phase 1 output (/speckit-plan)
│   ├── reconnect_fsm.hpp                 # ReconnectFsm driver + AwaitingResend transient + Heartbeat/TestRequest/Logout-timeout machinery
│   ├── resend_state.hpp                  # ResendState value type + helpers
│   ├── compid_authorization_policy.hpp   # CompIdAuthorizationPolicy value type + bound_principal + authorize(...) entry-point
│   ├── session_event.hpp                 # SessionEvent variant union — 5 new variants (peer_identity_bound / compid_authorization_failed / tls_validation_failed / credentials_rotated / sequence_numbers_reset)
│   ├── session_config_ext.hpp            # SessionConfig extension delta (3 new fields: reset_seqnum_policy / logout_disconnect_timeout_ms / compid_authorization_policy) — shipped form lives in include/fixpp/session/session_config.hpp
│   ├── session_ext.hpp                   # Session method extension delta (reload_credentials / logout(timeout)) — shipped form lives in include/fixpp/session/session.hpp
│   └── session_errors.hpp                # error::session_* variant family for 013 (slots 116..120; defs in core/error.hpp)
├── checklists/
│   └── requirements.md  # Spec-Kit quality checklist (from /speckit-specify; all-pass first iteration)
├── tasks.md             # NOT YET — produced by /speckit-tasks after Phase-4 Gate A
└── spec.md              # /speckit-specify + /speckit-clarify output (already authored)
```

### Source Code (repository root — submodule `library/`)

```text
library/
├── include/fixpp/
│   ├── core/
│   │   └── error.hpp                       # APPENDED — 5 new `error::session_*` variants at the contiguous block 116..120 immediately after 012's `transport_*` block (012 occupies 94..115 per shipped post-PR-#85 header: `transport_accept_cancelled = 115` per [2h §6.6]:1199; future ±N adjustment reconciled at /implement-time without re-running Gate A; never renumber existing slots; respects 005/008/009/010/011/012 prior pinning)
│   └── session/                            # EXTENDED MODULE — 4 new headers + 3 extended headers
│       ├── reconnect_fsm.hpp               # NEW — ReconnectFsm driver: per-attempt Transport mint via TransportFactory, AwaitingResend transient sub-state (a bool on Session, NOT a new fsm_state value), Heartbeat / TestRequest cadence (FR-003 / FR-004 / FR-005 / FR-006 / FR-007), Logout / force-disconnect timeout (FR-008)
│       ├── resend_state.hpp                # NEW — ResendState struct: outstanding ResendRequest tracking, inbound replay window, outbound replay window; lives across AwaitingResend; reset on protocol exit; FR-009..FR-015 consume
│       ├── compid_authorization_policy.hpp # NEW — CompIdAuthorizationPolicy value type (allow-list only per FR-023 / Clarifications Q3=A) + bound_principal + authorize(peer_identity const&, std::string_view asserted_compid) noexcept -> expected_t<bound_principal>; canonical-fixed principal extraction CN → SAN-DNS → SAN-URI → SHA-256-fingerprint per FR-022 / Clarifications Q2=A
│       ├── session_event.hpp               # NEW (extracted from 010's internal session_event.hpp if it was internal) — the SessionEvent variant union with 5 new variants from this feature: peer_identity_bound{cn, sans, sha256_fingerprint, cipher, bound_compid, principal_source} (FR-020); compid_authorization_failed{cn, asserted_compid, expected_compids[]} (FR-021); tls_validation_failed{variant: tls_verify_error, peer_endpoint, reason_string} (FR-026); credentials_rotated{old_sha256, new_sha256} (FR-032); sequence_numbers_reset{by_peer_request: bool} (FR-018)
│       ├── session.hpp                     # EXTENDED — adds `expected_t<void> reload_credentials(std::shared_ptr<fixpp::tls::cert_source> new_source) noexcept` (FR-030 / FR-031 / FR-032 / FR-033 — atomic swap at transport_factory::make(...) entry; in-flight handshake observes OLD; NEXT handshake observes NEW); adds `asio::awaitable<expected_t<void>> logout(std::chrono::milliseconds timeout = SessionConfig::logout_disconnect_timeout_ms) noexcept` (FR-008 — emits Logout(5), awaits peer reply for timeout, closes Transport, surfaces session_logout_disconnect_timeout if elapsed)
│       ├── session_config.hpp              # EXTENDED — adds 3 new fields per `[arch §5.6]` frozen-at-open carve-out: `reset_seqnum_policy: enum { bilateral_strict, bilateral_lenient, unilateral } = bilateral_strict` (FR-017 / Clarifications Q1=A); `logout_disconnect_timeout_ms: std::uint32_t = 2000` (FR-008 / Clarifications Q5=A); `compid_authorization_policy: CompIdAuthorizationPolicy` (default-constructed = empty allow-list = default-deny, FR-023)
│       ├── session_fsm.hpp                 # EXTENDED — adds `awaiting_resend: bool` transient flag on the Active state, NOT a new fsm_state enum value (preserves the 6-state ABI per `[arch §5.6]`); 6-state enum UNCHANGED
│       ├── seqnum_manager.hpp              # EXTENDED — adds FR-013 advance-without-store path for SequenceReset-GapFill; adds FR-015 PossDupFlag dedup
│       └── (unchanged: admin_messages.hpp, direction.hpp, sending_time.hpp, seqnum.hpp, message_store.hpp, message_store_factory.hpp, retrieve_visitor.hpp, security_profile.hpp, memory_store.hpp, memory_store_factory.hpp, file_store.hpp, file_store_factory.hpp, async_lock_via_session_executor.hpp, detail/, quickfix_compat/)
│
├── include/fixpp/transport/
│   └── listener.hpp                        # EXTENDED — adds `expected_t<void> asio_listener::reload_credentials(std::shared_ptr<fixpp::tls::cert_source> new_source) noexcept` (FR-030 acceptor half — symmetric with Session::reload_credentials initiator half per `[[feedback_half_restructure_symmetric_api]]`)
│
├── src/session/
│   ├── reconnect_fsm.cpp                   # NEW — Heartbeat/TestRequest cadence body; AwaitingResend entry/exit; ResendRequest emit; per-attempt Transport mint; Logout timeout body
│   ├── resend_state.cpp                    # NEW — ResendState helper out-of-line bodies (mostly header-only)
│   ├── compid_authorization_policy.cpp     # NEW — authorize(...) body + canonical-fixed principal-extraction helper
│   ├── session_event.cpp                   # NEW — event-emit helper out-of-line bodies (mostly header-only)
│   ├── session.cpp                         # EXTENDED — reload_credentials body + logout(timeout) body
│   ├── seqnum_manager.cpp                  # EXTENDED — FR-013 advance-without-store + FR-015 PossDupFlag dedup
│   └── (unchanged: admin_messages.cpp, sending_time.cpp, session_executor.cpp, file_store.cpp, file_store_factory.cpp, quickfix_compat/, CMakeLists.txt — modified to add the 4 new .cpp + 2 extended)
│
├── src/transport/
│   └── asio_listener.cpp                   # EXTENDED — adds reload_credentials body (symmetric with session.cpp body)
│
├── tests/session/                          # 10 new tests + 1 fuzz + 8-cell witness matrix
│   ├── test_reconnect_happy_path.cpp                          # FR-001 / FR-002 / US1 AC1 — initiator reconnect after transient disconnect; ResendRequest issue; recovery completes
│   ├── test_recovery_admin_span_gapfill.cpp                   # FR-011 / Edge: all-admin span → collapse to SequenceReset-GapFill(4)
│   ├── test_recovery_store_horizon.cpp                        # FR-012 / Edge: peer asks for [10..20], we have [15..20] → GapFill{NewSeqNo=15} then replay
│   ├── test_reset_seqnum_policy_matrix.cpp                    # FR-017 / US1 AC7 / Clarifications Q1=A — 3-mode matrix: bilateral_strict (mismatch → session_seqnum_reset_mismatch), bilateral_lenient (auto-mirror), unilateral (honour-unconditionally); 6 cells (3 modes × 2 initiator/acceptor roles)
│   ├── test_heartbeat_cadence_8cell.cpp                       # FR-003 / FR-004 / FR-005 / FR-006 / FR-007 / US1 AC4 — 8-cell matrix: 4 cells {emit-Heartbeat / emit-TestRequest / heartbeat_timeout / TestReqID-echo} × 2 executor modes {per_session_strand, direct_executor} per [2d §4.8]
│   ├── test_logout_timeout.cpp                                # FR-008 / US1 AC5 — initiator-graceful Logout; timeout cell → session_logout_disconnect_timeout; symmetric acceptor-force-disconnect cell
│   ├── test_compid_binding_default_deny.cpp                   # FR-023 / US2 AC6 — empty CompIdAuthorizationPolicy → ALL Logons rejected with session_compid_unauthorized; default-deny fails CLOSED
│   ├── test_compid_binding_principal_extraction.cpp           # FR-022 / US2 AC5 — canonical-fixed CN → SAN-DNS → SAN-URI → SHA-256-fingerprint; 4 cells (one per principal_source); principal_source surfaced in peer_identity_bound event
│   ├── test_compid_binding_symmetry.cpp                       # FR-024 / US2 AC4 — initiator-side rule applies symmetrically (peer's server cert ↔ peer's asserted TargetCompID); same policy contract; no half-restructure
│   ├── test_tls_validation_failed_all_variants.cpp            # FR-026 / FR-027 / SC-007 / US3 AC1+AC2+AC3 — fault-inject each of the 15 tls_verify_error variants from [2g §6.6]:986-1004; verify matching SessionEvent::tls_validation_failed{variant=...} surfaces; operator_config_error vs cert_expired distinction per FR-027 / 011 /clarify Q2
│   ├── test_reload_credentials_in_flight.cpp                  # FR-030 / FR-031 / FR-032 / FR-033 / US4 AC1+AC2+AC3 / Clarifications Q4=A — Active session unaffected; rotation deferred until in-flight handshake completes (atomic swap at transport_factory::make(...) entry); credentials_rotated event emits with old/new SHA-256
│   ├── test_session_invariant_counter_witness.cpp             # Invariant-counting regression test per `[[feedback_half_restructure_symmetric_api]]` — counter on `cert_source::load_credentials()` call site; assert calls=1 across N initiator + N acceptor handshakes after cert_source caching invariant
│   └── fuzz_session_recovery_admin_parse.cpp                  # libFuzzer; random byte streams via mock_transport into Session FSM through 2b Framer covering Logon-with-141=Y + ResendRequest + SequenceReset-GapFill + Heartbeat-with-TestReqID + Logout(5) admin parses; ASan + UBSan invariants; no crash / UAF / UB on adversarial inputs
│
├── tests/perf/
│   └── test_session_recovery_alloc_guard.cpp                  # Zero global new/delete on FSM Active ↔ AwaitingResend transition + Heartbeat emit; counting_resource + mallocnesia LD_PRELOAD DUAL gate per `[[feedback_tracking_pmr_resource_false_pass]]`
│
├── bench/session/
│   ├── bench_heartbeat_cadence.cpp                            # Heartbeat emit on Active ≤ 500 ns p99; CI fails > 5% regression
│   └── bench_compid_authorize.cpp                             # CompIdAuthorizationPolicy::authorize ≤ 5 µs p99; CI fails > 5% regression
│
├── spec/
│   ├── feature-catalogue.md                # APPEND 013-session-reconnect-binding row; flip S-005/006/014-FSM-half/024 + T-039/040/041 to `done` on merge
│   └── coverage-index.md                   # APPEND 013 ledger; rotate "Active feature" pointer
│
└── docs/src/
    ├── session-recovery-quickstart.md      # operator quickstart (mdBook) — recovery sub-protocol walkthrough
    └── compid-binding-quickstart.md        # operator quickstart (mdBook) — CompID binding + reload_credentials walkthrough
```

**Structure Decision**: extends the EXISTING `session/` module; no new module directory. The `tools/check_layers.py` config is UNCHANGED — `session/` is already registered. The `AwaitingResend` sub-state is a transient `bool` on `Session` (or a per-session `ResendState` field), NOT a new `fsm_state` enum value — this preserves the 6-state `fsm_state` enum's ABI per `[arch §5.6]` frozen-at-open rule and keeps the FSM transition matrix coherent with 005's published shape. The `reload_credentials` entry-point is a concrete method on `Session` (initiator) AND on `asio_listener` (acceptor), NOT a pure-virtual on any abstract base — preserves the `Transport`/`TlsTransport`/`Listener` ≤5 pure-virtual caps from 012 (Transport=5/5 AT cap, TlsTransport=1/5 sub-budget, Listener=1/5).

## Complexity Tracking

**No entries.** No constitution violations need justification. The `AwaitingResend`-as-transient-flag (NOT new `fsm_state` value) is rationale-recorded above and traces to `[arch §5.6]` frozen-at-open rule. The 5 new error slots respect the [const §X.2] append-only ABI discipline. The `reload_credentials`-as-concrete (NOT pure-virtual) follows the 012 `asio_listener::cancel` convention — value-typed policies + concrete methods, no new pluggable interfaces.

## Phase 0 — Outline & Research

**Output**: [`research.md`](research.md)

Phase 0's task is to resolve every NEEDS CLARIFICATION the Technical Context surfaced. There are NONE. The `/speckit-clarify` pass on 2026-05-28 resolved all 5 ambiguities (ResetSeqNumFlag policy / principal-extraction order / allow-list-vs-deny-list / reload-credentials concurrency / Logout timeout default) with per-Q reference-engine sweep across QuickFIX-cpp v1.16.0 / QuickFIX/J v3.0.1 / Fix8 v1.4.3 per `[[feedback_always_invoke_speckit_clarify]]`. Per `[const §XVII.1]`, `/speckit-clarify` non-skip is the discharge.

Research consolidates the binding decisions (D-1..D-N) from the 5 Clarifications + the consumed surfaces of 005 / 010 / 011 / 012, citing reference-engine evidence verbatim (file:line) for each per-Clarification decision so Gate A can audit grounding without re-running the sweep.

## Phase 1 — Design & Contracts

**Outputs**:
- [`data-model.md`](data-model.md) — every entity (E-1..E-7) with fields, ownership, allocator policy, lifetime, validation rules, state transitions (Session: Active → AwaitingResend → Active; Logon-141=Y handshake matrix; Logout timeout matrix; reload_credentials swap-point sequence diagram).
- [`contracts/`](contracts/) — 7 header files: 4 new (`reconnect_fsm.hpp`, `resend_state.hpp`, `compid_authorization_policy.hpp`, `session_event.hpp`), 2 extension-delta documents (`session_config_ext.hpp`, `session_ext.hpp`) recording the new field / method shapes, 1 error-family re-export (`session_errors.hpp`).
- [`quickstart.md`](quickstart.md) — operator-facing 4-scenario quickstart (recovery walk-through / CompID binding allow-list / TLS validation event observation / in-process credential rotation) + 1 developer scenario (FSM-via-mock-transport recovery seam).

**Agent context update**: `library/CLAUDE.md` "Active feature" pointer rotated from "(none currently in flight) — next planned: session-Phase-4" → "013-session-reconnect-binding" between the `<!-- SPECKIT START -->` and `<!-- SPECKIT END -->` markers per `/speckit-plan` step 5.

## Phase 2 — Tasks (NOT in scope of /speckit-plan)

`/speckit-tasks` produces `tasks.md` AFTER Phase-4 Gate A converges per `[const §XVII.1]` / `[pipeline.md step 4]`. The task list will consume the FR-to-seam mapping derived in this plan (Phase 1 §Test Plan, the 10 named tests + fuzz + perf alloc-guard + 2 benches), NOT re-derive the test set from the FRs per `[[feedback_speckit_pipeline_order_gate_a_before_tasks]]`. `/speckit-tasks` MUST also schedule explicit task rows for:
- Append `error::session_*` slots 116..120 to `include/fixpp/core/error.hpp` BEFORE any test that depends on them; cross-check actual file boundary remains at 115 (per Scale/Scope reconciliation rule).
- Update `library/spec/feature-catalogue.md` + `library/spec/coverage-index.md` per `[[feedback_feature_completeness_gate]]` — append 013 row; flip S-005/006/014-FSM-half/024 + T-039/040/041 to `done` AT MERGE-TIME (NOT pre-merge — close-out step 19a of pipeline.md).
- Remove the 4 in-code `2e-recovery` upgrade markers (FR-016).
- Drop the 44 `fixpp gap` cross-comms scenario tags (FR-016) — actual count may be ±N; `/speckit-implement`-time cross-check against the live catalogue per `[[project_2e_recovery_v1_upgrade_obligation]]`.
- Amend 005 FR-008 in-place to the recovery-active form (FR-009 / SC-008) — touches `specs/005-session-establishment-fsm/spec.md` directly; treat as high-risk completeness-audit row per `[[feedback_simplify_pass_catches_9th_burn]]`.

## Gate A

Phase-4 Gate A reviews the `specs/013-session-reconnect-binding/` bundle (spec + plan + research + data-model + quickstart + contracts + checklists) per `[const §XVII.1]` BEFORE `/speckit-tasks`. Reviews live under `library/../research/reviews/` (Codex pass + Opus adversarial pass per `[[feedback_gate_a_codex_dual_pass]]` precedent + Opus rewriter pass per `[[feedback_gate_a_structural_rec_and_judge_independence]]`).

**Gate A review priorities** (from this plan's load-bearing claims):
1. The `AwaitingResend`-as-transient-flag-NOT-fsm_state choice — is preserving the 6-state ABI worth the slight conceptual mismatch with the FIX-SL §4.3.2 "Resend protocol" being a logically-distinct sub-state? Reference engines (QFC `Session::nextResendRequest` / QFJ `Session.nextResendRequest()` / Fix8 `session::handle_resend_request`) all model recovery as a flag, not a state — alignment evidence in research.md.
2. The `reload_credentials`-as-concrete-NOT-virtual choice — is the operator API surface clean enough without a pluggable hook? 2j-controlplane RC#5 deferred the gRPC trigger; 013's IN-PROCESS variant is the v1.0 closure of `[2j §3.12]` reservation.
3. The 5-new-error-slot allocation at 116..120 — verify no ±N drift in the actual `include/fixpp/core/error.hpp` as of 013 Gate A start (might need a re-numbering carve-out per Round-3 hand-edit precedent from 012's Step F option 2).
4. The CompIdAuthorizationPolicy allow-list-only-in-v1.0 framing — is this a one-way restriction (adding deny-list later is backward-compatible per FR-023)? Verify no spec narrative implies otherwise.
5. The 44-cross-comms-scenario-drop count — is the catalogue audit AT MERGE-TIME (not pre-merge per pipeline.md step 19a) clearly documented?
6. The 005-FR-008-amendment-in-place — does treating it as a single edit (not a fork or version-bump) preserve audit-trail integrity? Cross-check with `[[project_005_phase8_completeness_false_pass]]` for any analogue concerns.
7. The principal-extraction canonical-fixed order — is the v1.0 lock-in (no per-binding operator override) correctly recorded as a one-way restriction in FR-022?

### Extension hooks

The `after_plan` hook (`speckit.git.commit`, optional) is available — invoke `/speckit-git-commit` if the user wants to commit `plan.md` + `research.md` + `data-model.md` + `contracts/` + `quickstart.md` as a single "speckit-plan output" commit before `/gate-a 013-session-reconnect-binding` runs.
