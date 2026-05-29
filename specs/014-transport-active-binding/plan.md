# Implementation Plan — 014-transport-active-binding

**Branch**: `014-transport-active-binding` | **Date**: 2026-05-29 | **Spec**: [spec.md](spec.md)

**Design anchors**: this slice has **NO Phase-2 design doc of its own** — it *realizes* three behaviours that 013 shipped as stubs, plus discharges the 013/012 Gate-B carry-forwards. The binding contracts are the **already-merged shipped surfaces** (re-emission discipline: on conflict, the cited shipped header wins; a divergence is a plan defect):
- `005-session-establishment-fsm` + `009-session-fsm-finalize` — the 6-state `fsm_state` machine + Logon handling + terminal-disconnected outcome (`[2e-FSM]`-equivalent ground truth).
- `010-session-cfg-lifetime` — the `SessionConfig` value type + the **`logon_peer_identity_override`** test seam (`include/fixpp/session/session_config.hpp:224`).
- `011-tls-policy` — `verify_peer` + `peer_identity` (CN / SAN-DNS / SAN-URI / leaf-fingerprint) + `last_handshake_sub_reason()` thread-local diagnostic; the `[const §XII.3]` signature-algorithm allow-list (consumed by FR-012's `sigalg_disallowed` cell).
- `012-2h-transport` (`[2h]`/`[2g]`) — `Transport` (5 pure-virtuals) + **`TlsTransport::async_handshake() -> awaitable<expected_t<handshake_result>>`** (`include/fixpp/transport/tls_transport.hpp:116`) with `handshake_result.peer_id : fixpp::tls::peer_identity` **owning** by value (`tls_transport.hpp:52-53`, held by the FSM per `tls_transport.hpp:40`); `TransportFactory::make(...)` + `TransportFactory::reload_credentials(...)` pure-virtual (`include/fixpp/transport/transport_factory.hpp:95-97`) + the `cert_source_slot_` atomic-swap / `cert_source_snapshot()` contract; `ReconnectPolicy` schedule-array + jitter + `max_attempts` + `delay_for_attempt(n)` (`include/fixpp/transport/reconnect_policy.hpp:30`).
- `013-session-reconnect-binding` — the `ReconnectFsm` driver shell (`include/fixpp/session/reconnect_fsm.hpp`), `CompIdAuthorizationPolicy::authorize(peer_identity const&, std::string_view) const noexcept -> expected_t<bound_principal>` (`src/session/compid_authorization_policy.cpp:296`), the TLS-outcome `SessionEvent` variant union incl. **`credentials_rotated{old_sha256, new_sha256}`** (`include/fixpp/session/session_event.hpp:102-105`, *defined but never emitted* — note at `session.hpp:156` "DEFERRED to 014"), and the in-process `reload_credentials` control plane.
- `constitution.md` v0.1 (Article IX coverage/sanitizer/ABI; XI concurrency/cancellation; XII security/TLS; XIV pluggable-interface caps; XV banned patterns; XVII Codex gates) + `architecture.md` v0.2 + decisions `2g`/`2h`/`2j`.

**Carved out to feature 015** (per `[[project_014_015_split]]`, resolved in spec `## Clarifications` 2026-05-29): the public multi-session **Initiator/Acceptor runtime engine** — the public accept/**connect-loop** component, the continuous inbound read-pump that reads the live transport and feeds `Session::on_inbound_frame`, the `SessionConfig`-keyed session registry, the acceptor `accept→Session-create→byte-feed` production path, removal of the `logon_peer_identity_override` test seam, and **full T-041 production fail-CLOSED closure for BOTH roles**. 014 wires the *initiator* live path and proves the binding through that path + the existing test seam; **T-041 stays `implementing` after 014**.

**Pipeline state** (authority = `.specify/pipeline.md`, not this compressed line): `/speckit-specify` (2026-05-29, reduced scope) → `/speckit-clarify` (2026-05-29, **2 Qs resolved** — auth-failure reconnect cause = reason-agnostic retry-to-cap; acceptor-binding boundary = initiator-live + test-seam, T-041 partial; reference-engine grounding QFC/QFJ per `[[feedback_always_invoke_speckit_clarify]]`) → **`/speckit-plan` (this doc, step 3)** → next per `[const §XVII.1]` / `[pipeline.md step 4]` is **Phase-4 Gate A** on the 014 bundle. Then step 5 `/speckit-tasks` → step 6 `/speckit-analyze` → step 7 `/speckit-checklist` → step 9 `/speckit-checklist-audit` (**MANDATORY gate**, blocks step 10; executor = checklist-auditor agent per `[pipeline.md [I]]`) → step 10 `/speckit-implement` → step 11 `/simplify` → step 12 `/speckit-verify` → step 14 Gate B.

## Summary

013 shipped the session-Phase-4 *control surface* with the **live-transport lifecycle stubbed**. 014 makes three of those behaviours real **at the session / reconnect-FSM / transport level**, then discharges five test/quality carry-forwards that were waived precisely because they needed a live TLS handshake (which the realized reconnect path now provides). It introduces **no new public type** — its only public-surface delta is one appended error-enum slot.

The three realizations (verified shipped reality, not plan prose):

1. **Live initiator reconnect (US1 / FR-001..005).** `ReconnectFsm::drive_reconnect_attempt()` today mints a transport via the factory and **immediately discards it** — `auto t = factory_->make(exec, ssl_cfg, nullptr); (void)t;` at `src/session/reconnect_fsm.cpp:124-131`, and the method is **not called by any production path**. 014 realizes it as the per-attempt **connect → handshake → identity-bind → rotation-emit → hand-back** coroutine wrapped in the bounded `ReconnectPolicy` retry loop: per attempt it reads the `cert_source` snapshot, calls `TransportFactory::make(...)`, drives `Transport::async_connect` then `TlsTransport::async_handshake` (the stub skips both), captures `handshake_result`, runs the authorization decision, and on success hands the authorized live transport + `handshake_result` back to the owning `Session` (re-binding the session's outbound path and re-driving Logon to Active). Failures (connect, handshake, **or authorization**) each consume one attempt and back off per the schedule until the cap → terminal-disconnected. Cancellation (`cancellation_type::total`) aborts an in-flight attempt and releases the partially-constructed transport with no leak.

2. **Live identity → authorization (US2 / FR-006..008).** The CompID authorization decision is already called on the Logon path at `src/session/session.cpp:958` (acceptor `LogonReceived`) and `:1758` (initiator `LogonSent` peer-Logon-ack), but **only when the test-only `logon_peer_identity_override` is set** (`if (cfg_.logon_peer_identity_override.has_value()) { authorize(...) } else { /* skip = fail-OPEN */ }`). 014 changes the identity **source** on the *initiator live path*: the real `handshake_result.peer_id` captured by `drive_reconnect_attempt` drives `authorize()`, closing the fail-OPEN hole for the initiator and removing the fabricated/skip arm there. The `logon_peer_identity_override` seam **remains** as the binding-logic test seam (off-list / on-list / absent); acceptor-side live binding + seam removal + full T-041 closure are 015. Fail-closed/permissive semantics, canonical CN→SAN-DNS→SAN-URI→SHA-256 extraction order, and the `compid_authorization_failed` + `session_compid_unauthorized` event/code shapes are **inherited unchanged** from 013 (FR-019/020/022).

3. **Real `credentials_rotated` emission (US3 / FR-009..011).** The `credentials_rotated{old_sha256, new_sha256}` event is defined (`session_event.hpp:102-105`, raw `std::array<std::byte,32>` leaf fingerprints) but **never emitted**. 014 emits exactly one event on the session strand at the next `drive_reconnect_attempt` after `reload_credentials` staged a new `cert_source` — **before** the new snapshot is passed to `make()` — carrying the **real** SHA-256 leaf fingerprints of our own old and new `cert_source` (computed from the loaded leaf DER, replacing 013's all-zero stub). No-op rotations (`old==new`) still emit, per 013 FR-032.

Plus the carry-forward hardening (US4 / FR-012..016), each now witnessable against a live TLS handshake fixture:

4. **FR-012** — add the `sigalg_disallowed` `sub_reason` cell to `tests/transport/test_tls_validation_failed_taxonomy.cpp` against an Ed25519/Ed448 (unknown-`EVP_PKEY`) cert fixture (today noted "not available in fixtures").
5. **FR-013** — (a) the `cert_source::load_credentials()` once-per-handshake counter witness (today **infeasible** with `mock_transport` per `tests/session/test_session_invariant_counter_witness.cpp:22-35` — zero loads; needs the real handshake) and (b) wire the `bench/transport/bench_tls_handshake_loopback.cpp` scaffold (012 RC#G; T029 TODO) to a live loopback fixture → establishes the **new** handshake baseline.
6. **FR-014** — extend `tests/transport/test_verify_peer_pmr_oom.cpp` (the `throw_on_nth_resource` boundary/mid/tail witness) with a **multi-SAN** cert fixture so the mid/tail allocation sites are genuinely exercised (012 RC#C; per `[[feedback_trap_throw_pmr_witness_enumerate_sites]]`).
7. **FR-015** — re-label the `tests/fuzz/fuzz_transport_handshake.cpp` catalogue scope entry to its actual post-MVP scope (012 RC#I; doc/catalogue only).
8. **FR-016** — mint a dedicated **`error::session_seqnum_too_high = 120`** (next free slot) and replace the vestigial slot-74 stand-in (`co_return std::unexpected(error::session_test_request_unanswered);` at `src/session/seqnum_manager.cpp:71-78`), updating the comment, the test assertion (`tests/session/seqnum_manager_test.cpp:145-150`), and the contract note. Slot 70 stays a permanent numeric hole; slot 74 keeps its real meaning (no caller reads the code today — the 3 sites at `session.cpp:904/1261/1703` discard it and disconnect — so this is a semantic/ABI-hygiene fix with zero behavioural change).

**Catalogue rows owned / co-owned**:
- **`T-041`** (CompID↔TLS-identity binding) — **advances on the initiator live path; STAYS `implementing`** after 014 (full production fail-CLOSED closure for both roles = 015).
- **Carry-forward catalogue updates**: the FR-012 `sigalg_disallowed` cell, the FR-013 counter + handshake-bench witnesses, the FR-014 PMR-OOM depth cell, the FR-015 fuzz-scope **re-label**, and the FR-016 slot-74 taxonomy-hygiene item all flip from `waived`/`carry-forward` to `done`. `feature-catalogue.md` + `coverage-index.md` get the 014 row per `[[feedback_feature_completeness_gate]]`.
- **No row flips to `done` by this feature alone** beyond the carry-forward hygiene items; the headline session rows (S-005/006/014/024, T-039/040) already flipped in 013.

**Branch base**: `014-transport-active-binding` rooted on post-PR-#86-merge `main` of the library submodule (013 squash `bd84e08`; post-merge `[const §XV.9]` fix `8e2d362`). **Inbound binding contracts** (all merged): 005/009 (PR #82), 010 (#83), 011 (#84), 012 (#85), 013 (#86). Carry-forwards this feature **discharges**: the 012 RC#C/RC#G/RC#I waivers + the 013 slot-74 stand-in + the 013 sigalg/counter items (per `[[project_013_carryforwards_to_014]]`). Codecov DA/BRDA + W-format/W-tidy/W-2/W-3 lint carry-forwards remain a separate `chore/` branch per `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]` (NOT 014 scope — Out of Scope §3).

**Gate A required**: **yes** — three Appendix A triggers fire: **Security** (the live identity→authorization binding is the application-layer security backbone; the `sigalg_disallowed` cell exercises the `[const §XII.3]` allow-list), **Session FSM** (the reconnect-lifecycle realization + the auth-failure-as-attempt loop semantics), and **Error semantics** (the new `session_seqnum_too_high = 120` slot). All four mandatory controls per `[const §XVI.3]` / `[const §XVII.1]`: `/clarify` ✓ 2026-05-29, `/analyze` (step 6), Codex Gate A (step 4), user `/plan` sign-off.

## Technical Context

**Language/Version**: C++23 (`[const §II.1]`). `asio::awaitable<T>` coroutines, `core::expected_t`, `std::pmr`, `std::span`, `std::variant`, `std::array<std::byte,32>` (leaf fingerprints), `std::shared_ptr<cert_source>` (atomic-swap snapshot ownership carried from 012/013), `[[nodiscard]]` on every `expected_t<T>` return, ASIO native cancellation slots. **No new language facility beyond what 011/012/013 already use.**

**Primary Dependencies**: **No new Conan rows; no new public type.** All consumed surfaces are already shipped:
- **`fixpp::transport`** (012 — LOCKED): `Transport` (5 pure-virtuals: `async_connect`/`async_read_some`/`async_write`/`close`/`cancel`), `TlsTransport` (+1: `async_handshake`), `handshake_result{peer_id, ...}` (`tls_transport.hpp:52`), `TransportFactory::make(asio::any_io_executor, tls::SslCtxConfig, std::pmr::memory_resource*) -> expected_t<unique_ptr<Transport>>` + `TransportFactory::reload_credentials(shared_ptr<cert_source>) noexcept` pure-virtual (count already **2/5** from 013 — 014 adds **none**) + `cert_source_snapshot()` (atomic `load(acquire)`), `ReconnectPolicy` (`reconnect_policy.hpp:30`: `schedule`/`max_attempts`/`delay_for_attempt(n)` + jitter — already constructed at `ReconnectFsm` ctor; 014 walks it). The `[2h]` in-flight-exclusivity + `cancellation_type::total → transport_*_cancelled` contracts are BINDING and consumed verbatim.
- **`fixpp::tls`** (011 — LOCKED): `cert_source::load_credentials() -> awaitable<expected_t<local_credentials>>` (`cert_source.hpp:32-41`), `peer_identity`, `verify_peer`, `last_handshake_sub_reason()`, the `[const §XII.3]` signature-algorithm allow-list (FR-012 exercises its `sigalg_disallowed` rejection).
- **`fixpp::session`** (005/009/010/013 — extended, not redesigned): `ReconnectFsm` (`drive_reconnect_attempt` realized), `Session::on_inbound_frame(span<const std::byte>)` (`session.cpp:862` — the inbound seam the test harness/engine feeds), the `transport_send_ : std::function<void(span<const std::byte>)>` outbound callback (`session.hpp:496`), `CompIdAuthorizationPolicy::authorize`, `SessionEvent::credentials_rotated` (now emitted), `Session::recent_events()` ring, `SeqnumManager::check_inbound`.
- **`fixpp::core`**: `expected_t`, `error` (014 **appends one** variant `session_seqnum_too_high = 120`; never renumbers; slot 70 stays a hole), `trap_throw`, `Clock`/`mock_clock`, cancellation plumbing.
- **OpenSSL** — same row 011/012 pinned; 014 uses it only to compute the SHA-256 of our own leaf DER for `credentials_rotated` (no new abstraction; **no new `cert_source` pure-virtual** — fingerprint is computed from the already-returned `local_credentials` leaf, keeping `[const §XIV.2]` caps untouched).
- **GoogleTest 1.17.0** + **Google Benchmark 1.9.5** + **libFuzzer** (existing `fuzz_transport_handshake` harness — re-labelled, not extended). The new live-handshake integration tests link `asio_tls_transport` over loopback (the fixtures already exist under `tests/tls/fixtures/`, addressed via `FIXPP_TLS_FIXTURE_DIR`); recovery/FSM-only seams keep using `mock_transport`.

**Storage**: N/A. No `MessageStore` contract change (013 owns recovery; 014 is connect/handshake/identity/rotation wiring).

**Testing**: GoogleTest + Google Benchmark, TDD red-green-refactor per `[const §VII.1/§VII.3]`. **Two transport seams, deliberately split**:
- **Live `asio_tls_transport` over loopback** for: the reconnect happy-path + handshake-failure/cap + cancel-mid-handshake (US1), the live-identity→authorize binding proof (US2 — real `handshake_result.peer_id`), the `credentials_rotated` real-fingerprint emit (US3), and the now-feasible FR-013 counter + handshake-bench witnesses. This is the fixture the carry-forwards were waiting for.
- **`mock_transport`** for the binding-logic test seam (US2 off/on/absent identities via `logon_peer_identity_override`) and any FSM step that does not require real bytes.

Deterministic time via `mock_clock` for backoff-schedule/cap cells. Sanitizer matrix per `[const §IX.2]`: ASan + UBSan + **TSan** (critical — the reconnect loop, the cancel-mid-handshake release, and the cross-strand `credentials_rotated` emit all touch concurrency) + Coverage; GCC Release sanity. **`[const §XV.9]` corpus regression watch** per `[[feedback_awaitable_header_mutex_include_edge]]`: 014 adds includes into the awaitable-corpus headers (`reconnect_fsm.hpp`/`session.hpp`); `/speckit-verify` MUST run the **unfiltered** Tier-1 ctest (or `-L sync`), never a name-scoped `-R` subset (lesson from 013 close-out `8e2d362`, encoded in SC-007).

**Target Platform**: same Tier-1 matrix as 005–013. **No C-ABI surface added** — `[const §IX.5]` abidiff N/A. The new `session_seqnum_too_high = 120` is a **C++ enum** value, not a `fixpp_error_t` C-ABI symbol (the 2i C-ABI error mapping is a later feature); `[const §X.2]` `nm` check inherited (no new `extern "C"`).

**Project Type**: C++23 library, **extends the session module + touches transport tests/bench** — no new module directory; `tools/check_layers.py` unchanged. Edits are concentrated in `src/session/reconnect_fsm.cpp`, `src/session/session.cpp`, `src/session/seqnum_manager.cpp`, `include/fixpp/core/error.hpp` (one slot), and the `tests/`/`bench/` carry-forward surfaces.

**Performance Goals** (per `[const §VIII.2]`):
- **Reconnect attempt** (`drive_reconnect_attempt`): **cold path** — connect + TLS handshake dominate (network/OpenSSL bound). No hot-path budget; no `±5%` regression gate (there is no prior baseline — `drive_reconnect_attempt` was a no-op stub).
- **Handshake bench (FR-013b)**: the `bench_tls_handshake_loopback` scaffold is wired to measure 1-RTT loopback handshake (scaffold target ~≤10 ms p99). This **establishes the first real baseline** (012 RC#G only scaffolded it); it is a measurement baseline, **not** a regression gate against a prior number this PR. Future PRs gate ±5% against it.
- **`credentials_rotated` fingerprint compute**: cold path (one SHA-256 over the leaf DER per rotation, at reconnect). Not benched.
- **No hot-path (`on_inbound_frame` / Heartbeat) allocation or latency change** — 014 adds nothing to the steady-state dispatch path. The FR-013a counter witness asserts `load_credentials()` fires **exactly once per handshake** (not per-attempt redundantly).

**Constraints**:
- **`[const §XI.2]` ASIO native cancellation end-to-end.** `drive_reconnect_attempt`, `async_connect`, `async_handshake`, and the inter-attempt backoff sleep all `co_await asio::this_coro::cancellation_state`; `cancellation_type::total` aborts and releases the in-flight transport → matching `transport_*_cancelled`. **Heed `[[feedback_asio_cospawn_total_cancellation_default]]`**: `co_spawn` defaults to terminal-only cancellation — the reconnect coroutine MUST reset to `enable_total_cancellation()` or a `total` stop silently hangs with no diagnostic. SC-004 (no leak across N failed/cancelled attempts) is the ASan/sanitizer witness.
- **`[const §VIII.5]` / §XV.1 zero global `new`/`delete` on the hot path** — unchanged: 014 touches only the cold connect/handshake/rotation paths. PMR throws on the cold paths route through `[2a §4.2]` `trap_throw` and surface as `*_*` variants; no PMR throw escapes the public surface (FR-014's PMR-OOM witness verifies this on the multi-SAN DN-extract path).
- **`[const §XIV.2]` pluggable-interface caps UNCHANGED** — 014 adds **no** pure-virtual to any pluggable interface (`Transport` stays 5/5, `TlsTransport` +1 as shipped, `TransportFactory` stays 2/5). The leaf-fingerprint is computed from the already-returned `local_credentials`, **not** via a new `cert_source` method.
- **`[const §X.4]` ABI append-only** — `session_seqnum_too_high = 120` appends after 013's `session_invalid_argument = 119`; `/speckit-implement`-time cross-check confirms the boundary is still 119 (no ±N drift) before assigning 120; never renumber; slot 70 remains a permanent hole; slot 74 retains its meaning.
- **`[const §XII]` security** — fail-CLOSED on off-list/absent identity under a binding policy is inherited from 013 unchanged; 014 only swaps the identity *source* (stub → live `handshake_result.peer_id`) on the initiator path. The `sigalg_disallowed` cell asserts the `[const §XII.3]` allow-list rejects an Ed25519/Ed448 leaf.
- **`[const §XI.4]` per-session strand** — the `credentials_rotated` emit lands on the session strand (FR-009); the `cert_source` snapshot read is the strand-free atomic `load(acquire)` per 012/013.
- **Reason-agnostic reconnect (Clarifications Q1)** — an `authorize()` failure on a reconnect attempt consumes exactly one attempt and is retried to the cap identically to a connect/handshake failure; **no** fail-fast and **no** new terminal cause/code. Matches QFC/QFJ reason-agnostic reconnect (reference-engine sweep).
- **014/015 boundary (Clarifications Q2)** — the continuous inbound read-pump + public connect-loop + registry are 015. 014 proves "session resumes" at the FSM/attempt level: `drive_reconnect_attempt` produces the authorized live transport + re-drives Logon to Active, and the loopback integration test feeds the post-handshake inbound frames via the existing `on_inbound_frame` seam (as every session test does). 014 introduces no public connect-loop component.
- **Half-restructure discipline** per `[[feedback_half_restructure_symmetric_api]]` — 014 *deliberately* fixes only the **initiator** live path and **documents** the acceptor-side deferral to 015 (Clarifications Q2 + spec FR-008). This is a documented, clarified asymmetry, NOT an undocumented half-fix; the acceptor binding stays proven via the test seam until 015 removes it.
- **Production-shaped entry-point exercise** per `[[feedback_subagent_phase_verification_two_traps]]` — US1/US2/US3 tests drive real bytes through `async_handshake` + `on_inbound_frame` over the loopback fixture; `SUCCEED()`-placeholder or method-poking-only tests count as MISSING coverage in the completeness audit.

**Scale/Scope**: small, surgical — **no new public type, no new header**. Estimated footprint:
- **`include/fixpp/core/error.hpp`** — **+1** enum variant (`session_seqnum_too_high = 120`) + the slot-74 hole/meaning comment update.
- **`src/session/reconnect_fsm.{hpp,cpp}`** — realize `drive_reconnect_attempt`: the per-attempt connect/handshake/identity/rotation pipeline + the `ReconnectPolicy` retry-to-cap loop + cancellation/release + the FSM-held `{last_active_source_ : shared_ptr<cert_source>, last_active_fp_ : array<byte,32>}` rotation-detect state (strong-ref owning member per `[[feedback_weak_ptr_cache_needs_owning_context]]`). ~250–400 lines.
- **`src/session/session.cpp`** — at `:958`/`:1758` swap the authorize identity *source* to the FSM-held live `handshake_result.peer_id` (keep the `logon_peer_identity_override` seam arm); wire `drive_reconnect_attempt`'s outcome (rebind `transport_send_`, store peer_id, re-drive Logon). ~120–200 lines.
- **`src/session/seqnum_manager.{hpp,cpp}`** — FR-016: return `session_seqnum_too_high` + comment. ~5 lines.
- **Tests** (`tests/session/` + `tests/transport/`): live-reconnect happy-path + handshake-fail/cap + cancel-mid-handshake (US1); live-identity-binding + binding-logic-seam off/on/absent (US2); `credentials_rotated` real-fingerprint + no-op (US3); FR-012 `sigalg_disallowed` cell; FR-013 counter (now feasible) ; FR-014 multi-SAN PMR-OOM depth; FR-016 assertion flip. ~1.0–1.4k lines test + fixtures (Ed25519/Ed448 + multi-SAN cert added under `tests/tls/fixtures/`).
- **Bench**: wire `bench/transport/bench_tls_handshake_loopback.cpp` (existing scaffold).
- **Docs/catalogue**: `feature-catalogue.md` + `coverage-index.md` 014 row + carry-forward flips; FR-015 fuzz-scope re-label; `quickstart.md` (Phase-1 output).

## Constitution Check

*GATE: passed before Phase 0; re-checked post-Phase 1 (below). No violations → no Complexity Tracking entries.*

| Article | Clause | This feature | Status |
|---|---|---|---|
| II.1 | C++23 | No new facility beyond 011/012/013 | ✓ |
| VI | Spec coverage | T-041 advances (stays `implementing`); carry-forward catalogue rows flip `done`; `feature-catalogue.md` + `coverage-index.md` updated per `[[feedback_feature_completeness_gate]]` | ✓ |
| VII.1/VII.3 | TDD red-green-refactor | Tests-first per phase | ✓ (procedural; enforced at /tasks + /implement) |
| VII.5 | Conformance corpus | No regression; the live path lets previously-`gap` handshake cases run | ✓ |
| VII.6 | Interop | The QFC/QFJ/Fix8 matrix needs 014+**015** then a chore branch (per `[[project_release_interop_quickfix_fix8]]`); 014 unblocks, does not deliver it | ✓ (procedural — unblock obligation) |
| VII.7 / IX.4 | Fuzz on parser-touching surfaces | **No new parser-touching code**; the existing `fuzz_transport_handshake` harness is re-labelled (FR-015), not extended | ✓ |
| VIII.2 | Perf regression gate | Reconnect = cold path; handshake bench establishes a **new** baseline (012 RC#G scaffold), not a regression check this PR | ✓ |
| VIII.5 | Zero new/delete on hot path | 014 touches only cold connect/handshake/rotation paths; FR-013a counter asserts once-per-handshake load | ✓ |
| IX.1 | ≥95% line / ≥85% branch on touched modules | Targeted via test plan; **YELLOW carve-out** likely for OpenSSL-level handshake-failure / TLS-abort injection paths (hard to reach without fault hooks) — recorded per `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]` (PR #73/#74/#84/#85 precedent) | ✓ (planned; documented if YELLOW) |
| IX.2 | Sanitizer Tier 1 ASan+UBSan+TSan | All three; TSan central to the reconnect-loop + cancel-release + strand-emit; SC-004 is the no-leak witness | ✓ |
| IX.5 / X.2 | abidiff / no `extern "C"` | No C-ABI surface emitted; new slot 120 is a C++ enum value only | ✓ (N/A) |
| X.4 | ABI append-only error slots | `session_seqnum_too_high = 120` appended; boundary-at-119 cross-checked at /implement; slot 70 hole + slot 74 meaning preserved | ✓ |
| XI.2 | ASIO native cancellation | connect/handshake/backoff honour `total`; **`enable_total_cancellation` reset required** per `[[feedback_asio_cospawn_total_cancellation_default]]` | ✓ |
| XI.4 | Per-session strand | `credentials_rotated` emit on strand; snapshot read is strand-free atomic | ✓ |
| XII | Security & TLS | Fail-CLOSED inherited unchanged from 013; live `peer_id` source on the initiator path closes that path's fail-OPEN hole; `sigalg_disallowed` cell exercises the `[const §XII.3]` allow-list; no coalescing of the `tls_*` master-enum | ✓ (central — see Phase-0 §R2/§R5) |
| XIV.2 | Pluggable ≤5 pure-virtuals | **No new pure-virtual** (`Transport` 5/5, `TlsTransport` +1 shipped, `TransportFactory` 2/5); leaf fingerprint computed from `local_credentials`, not a new `cert_source` method | ✓ |
| XV.9 | No `std::mutex` in coroutine context | Strand + ASIO cancellation only; **unfiltered Tier-1 ctest** in verify per `[[feedback_awaitable_header_mutex_include_edge]]` (new includes into awaitable-corpus headers) | ✓ |
| XV.15 | No drop-oldest on app/session path | Inherited from 012/013 verbatim | ✓ |
| XVI.3 | `/clarify` mandatory (security/FSM/error) | ✓ 2026-05-29, 2 Qs, reference-engine grounding | ✓ |
| XVI.7 | `/simplify` before `/speckit-verify` | Procedural (step 11 → 12) | ✓ (procedural) |
| XVII.1 | Gate A blocks `/tasks` | Next gate after this `/plan` (step 4) | ✓ (procedural — not yet executed) |
| XVII.8 | `/speckit-verify` mandatory; paired-evidence labels | Procedural (step 12, pre-Gate-B); unfiltered ctest per SC-007 | ✓ (procedural) |

**No constitution violations to track.** The single ABI append (slot 120) respects `[const §X.4]`; the deliberate initiator-only binding asymmetry is a *clarified, documented* boundary (Q2), not a half-restructure defect.

## Project Structure

### Documentation (this feature)

```text
specs/014-transport-active-binding/
├── plan.md              # This file (/speckit-plan output, step 3)
├── research.md          # Phase 0 output — 6 decisions (R1–R6)
├── data-model.md        # Phase 1 output — entities + design records E-1..E-5
├── quickstart.md        # Phase 1 output — initiator-reconnect + binding + rotation walkthrough
├── contracts/           # Phase 1 output
│   ├── error_slots.hpp           # The ONE genuine public delta: error::session_seqnum_too_high = 120 (+ slot-74/70 notes)
│   └── realized-behavior.md      # Behavioural contracts for the 3 realized stubs (no new signatures)
├── checklists/
│   └── requirements.md  # from /speckit-specify
├── tasks.md             # NOT YET — step 5, after Gate A
└── spec.md              # /speckit-specify + /speckit-clarify (authored)
```

### Source Code (repository root — submodule `library/`)

```text
library/
├── include/fixpp/
│   ├── core/
│   │   └── error.hpp                  # APPENDED — error::session_seqnum_too_high = 120 (after session_invalid_argument=119); slot-74 comment: retire the too-high MISUSE, keep slot 74's real meaning; slot 70 permanent hole
│   └── session/
│       ├── reconnect_fsm.hpp          # EXTENDED — drive_reconnect_attempt realized signature/contract; FSM-held rotation-detect state {last_active_source_, last_active_fp_} (strong-ref member)
│       └── session.hpp                # (comment at :156 "credentials_rotated DEFERRED to 014" resolved; reload_credentials forwarding unchanged)
│
├── src/session/
│   ├── reconnect_fsm.cpp              # REALIZED — replaces the make()+`(void)t` stub (:124-131) with connect→handshake→authorize→rotation-emit→hand-back + ReconnectPolicy retry-to-cap loop + cancellation/release
│   ├── session.cpp                    # EXTENDED — :958/:1758 authorize identity SOURCE = live handshake_result.peer_id (keep override seam arm); install reconnect outcome (rebind transport_send_, re-drive Logon)
│   └── seqnum_manager.cpp             # EXTENDED — :77 return session_seqnum_too_high + comment (FR-016)
│
├── tests/session/
│   ├── test_reconnect_live_happy_path.cpp        # FR-001/002 / US1 AC1 — loopback-TLS: drop → reconnect → handshake → resume to Active
│   ├── test_reconnect_backoff_cap.cpp            # FR-002/003 / US1 AC2 + Clarifications Q1 — failing peer (connect-fail / handshake-fail / AUTH-fail) each consumes one attempt; terminates at cap; no infinite retry
│   ├── test_reconnect_cancel_mid_handshake.cpp   # FR-004 / US1 AC3 / SC-004 — stop/total mid-handshake → abort + release in-flight transport; ASan no-leak across N
│   ├── test_live_identity_binding.cpp            # FR-006 / US2 AC1 — real handshake_result.peer_id drives authorize() (no fabricated payload on the live path)
│   ├── test_compid_binding_seam.cpp              # FR-007 / US2 AC2 — logon_peer_identity_override seam: off-list/absent → fail-closed (session_compid_unauthorized + compid_authorization_failed); on-list → admit
│   ├── test_credentials_rotated_emit.cpp         # FR-009/010/011 / US3 — real old/new leaf SHA-256, emitted on strand BEFORE make(); no-op rotation emits old==new
│   ├── test_session_invariant_counter_witness.cpp# FR-013a (EXTEND existing) — load_credentials() == 1 per handshake, now over the LIVE fixture (was infeasible w/ mock)
│   └── seqnum_manager_test.cpp                    # FR-016 (EXTEND :145) — assertion flips to session_seqnum_too_high
│
├── tests/transport/
│   ├── test_tls_validation_failed_taxonomy.cpp   # FR-012 (EXTEND) — add the sigalg_disallowed sub_reason cell against an Ed25519/Ed448 leaf fixture
│   └── test_verify_peer_pmr_oom.cpp              # FR-014 (EXTEND) — multi-SAN cert fixture → throw_on_nth_resource exercises mid/tail SAN sites, not only boundary
│
├── tests/tls/fixtures/                            # NEW fixtures: leaf_ed25519.pem / leaf_ed448.pem (FR-012); leaf_multi_san.pem (FR-014)
│
├── bench/transport/
│   └── bench_tls_handshake_loopback.cpp          # FR-013b (WIRE existing scaffold) — live loopback 1-RTT handshake baseline
│
└── tests/fuzz/
    └── fuzz_transport_handshake.cpp              # FR-015 — catalogue/scope label re-label only (no code change to the harness body)
```

**Structure Decision**: 014 is an in-place realization of stubbed behaviour in the existing `session` module plus carry-forward witnesses in `transport`/`bench`/`fuzz` tests. No new module, no new public type, no `tools/check_layers.py` change. The only genuine public-surface delta is `error::session_seqnum_too_high = 120` (captured in `contracts/error_slots.hpp`).

## Complexity Tracking

> No `Constitution Check` violations → **no entries**. The deliberate initiator-only identity-binding asymmetry is a clarified, documented 014/015 boundary (Clarifications Q2), not an unjustified complexity; the single error-slot append is standard `[const §X.4]` ABI hygiene.
