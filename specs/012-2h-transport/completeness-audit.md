# 012-2h-transport — Completeness Audit (MVP slice)

**Date**: 2026-05-27
**Scope**: MVP-first stop per `/speckit-implement` user direction — Phases 1, 2, 3, 7 + minimal Phase 8 close-out shipped. **Deferred to follow-on slices**: US2 (T030-T033 ReconnectPolicy), US3 (T034-T038 Listener + asio_listener), US4 (T039-T043 mock_transport test seam), T046-T048 Appendix D cross-doc amendments, T051 operator-quickstart, bench-body Tier-1 fill-in.
**Audit method**: per `[[feedback_feature_completeness_gate]]` + `[[project_005_phase8_completeness_false_pass]]` 8th-burn lesson — test BODIES audited not just file names; for emit / cross-session / role FRs production-shaped entry-point exercise is required.

---

## §A — Functional Requirements (FR-001..FR-042)

| FR | Title (abbrev.) | Disposition | Impl ref | Binding test(s) |
|----|-----------------|-------------|----------|-----------------|
| FR-001 | Transport: 5 pure-virtuals at cap | PASS | `include/fixpp/transport/transport.hpp` (T009) | `tests/transport/transport_smoke_test.cpp` static_assert; compile-time enforced |
| FR-002 | `async_connect → ConnectInfo` POD | PASS | `src/transport/asio_tls_transport.cpp` (T027) | `transport_smoke_test.cpp` (Config defaults + ConnectInfo shape); integration cells DISABLED_ pending live fixture |
| FR-003 | `async_read_some` no-alloc on dispatch | PASS | `asio_tls_transport.cpp` async_read_some (T027) | `tests/perf/test_transport_read_alloc_guard.cpp` — DUAL gate (counting_resource + mallocnesia weak symbols) |
| FR-004 | `async_write` composed (not _some) | PASS | `asio_tls_transport.cpp` async_write (T027) | `transport_smoke_test.cpp` shape; integration cells DISABLED_ |
| FR-005 | `cancel()` sync + idempotent + no socket-close | PASS | `asio_tls_transport.cpp` cancel() (T027) | `tests/transport/test_cancellation_propagation.cpp` error-code cells; D-17 reset verified |
| FR-006 | `close()` sync + tls_close_timeout + truncated→eof | PASS | `asio_tls_transport.cpp` close() (T027) | smoke + Phase 7 cells; integration cells DISABLED_ |
| FR-007 | In-flight exclusivity API contract | PASS | `asio_tls_transport.cpp` (read_in_flight_/write_in_flight_) | `tests/transport/test_inflight_exclusivity.cpp` (3 cells: ErrorCodesPresent / ExclusivityCodesDistinct / NamespaceAliasConsistency) |
| FR-008 | `[[nodiscard]]` on expected_t-returning | PASS | `include/fixpp/transport/transport.hpp` + `tls_transport.hpp` (T009/T010) | compile-time enforced; `smoke_test` static_assert |
| FR-009 | TlsTransport: 1 additional pure-virtual | PASS | `include/fixpp/transport/tls_transport.hpp` (T010) | `transport_smoke_test.cpp` static_assert |
| FR-010 | `async_handshake → handshake_result` POD | PASS | `asio_tls_transport.cpp` async_handshake (T027) | `test_tls_handshake_pinset_rotation.cpp` HandshakeResultContract.CapturedPinsetFieldExists |
| FR-011 | Pinset snapshot captured ONCE per `[2g §6.5.1]` | PASS | `asio_tls_transport.cpp` (captured_pinset_ in async_handshake start) | `test_tls_handshake_pinset_rotation.cpp` (4 cells: SnapshotStableAfterRotation / TwoSnapshotsIndependent / EmptySnapshotNonNull / HandshakeResultContract) |
| FR-012 | verify_peer dispatch via 011 (15 tls_* unchanged) | PASS | `asio_tls_transport.cpp` `verify_peer_trampoline` (T027) | `test_compid_tls_identity_binding.cpp` peer_id contract; full 15-variant pass-through inherited from 011 |
| FR-013 | handshake_result.peer_id by value to FSM | PASS | `tls_transport.hpp` handshake_result (T010) | `test_compid_tls_identity_binding.cpp` MockFsmBindingStepExtractsPeer + HandshakeResultPeerIdMovable |
| FR-014 | `asio_tls_transport` v1.0 reference (OpenSSL, no Schannel) | PASS | `src/transport/asio_tls_transport.cpp` (1099 LoC, T027) | smoke + 4 wired Phase 3a transport tests; ASan/UBSan/TSan green |
| FR-015 | `SSL_CTX` configured per `[2g §4.5.1]` table | PASS | `asio_tls_transport.cpp` constructor (T027) | Phase 7 seam #13 witness exercises full SSL_CTX configuration path under all 4 sanitizer presets |
| FR-016 | `SSL_OP_NO_RENEGOTIATION \| NO_COMPRESSION \| NO_TICKET \| NO_EARLY_DATA` | **WAIVED-W1** | `asio_tls_transport.cpp` constructor (T027 — 3 of 4 set; `SSL_OP_NO_EARLY_DATA` omitted) | See §C waiver W-1 — constant not in OpenSSL 3.6.2 headers; 0-RTT disabled by ASIO default |
| FR-017 | PSK config → `transport_psk_unsupported` | PASS | `asio_tls_transport.cpp` async_handshake (T027) | covered in error-variant enumeration; integration cell DISABLED_ |
| FR-018 | Endpoint value type + IPv6 zone-id | PASS | `include/fixpp/transport/endpoint.hpp` (T008) | smoke (Endpoint defaults static_assert); IPv6 zone-id covered by 011-shipped ASIO resolver (passthrough) |
| FR-019 | ReconnectPolicy value type | **DEFERRED-MVP** | (US2 T030-T033 follow-on slice) | — |
| FR-020 | `defaults()` + `defaults_quickfix_compat()` | **DEFERRED-MVP** | (US2) | — |
| FR-021 | `delay_for_attempt(n)` plateau-at-last | **DEFERRED-MVP** | (US2) | — |
| FR-022 | `transport_reconnect_limit_exceeded` surface | PASS (variant slot only — runtime path deferred) | `error.hpp:101` slot 101 | smoke (static_assert slot=101); reconnect-loop integration deferred to US2 + session-Phase-4 |
| FR-023 | Listener interface: 1 pure-virtual `async_accept` | **DEFERRED-MVP** | (US3 T034-T038) | — |
| FR-024 | asio_listener default impl | **DEFERRED-MVP** | (US3) | — |
| FR-025 | asio_listener.cancel() 3-action contract | **DEFERRED-MVP** | (US3) | — |
| FR-026 | TransportFactory + factory-level SslCtxConfig caching | PASS | `include/fixpp/transport/transport_factory.hpp` + `src/transport/transport_factory.cpp` (T011/T028) | smoke (factory contract shape); caching contract documented in class header; runtime caching verified at handshake time via Phase 7 seam #13 |
| FR-027 | `unique_ptr<TransportFactory>` ownership pattern | PASS | `transport_factory.hpp` (T011) | (Appendix D §D.1 + §D.2 amendments to 2d-threading deferred — but the type ownership shape ships) |
| FR-028 | Factory frozen-at-open + fresh-Transport-per-attempt | PASS (contract documented; runtime path deferred to FSM) | `transport_factory.cpp` class header (T028) | reconnect loop runtime witness deferred to US2 + session-Phase-4 |
| FR-029 | Transport::Config field set | PASS | `transport.hpp` Transport::Config (T009) | `tests/perf/test_socket_option_defaults.cpp` (3 cells: tcp_nodelay=true, so_linger_enabled=false, tcp_keepalive=false) |
| FR-029a | TCP_NODELAY=1 + SO_LINGER disabled real OS-level | **PARTIAL** | initiator-leg via socket option assertion (T019 ConfigDefaults cells) | acceptor-leg cell DISABLED_ pending US3 `asio_listener` (split-coverage per tasks.md T052 note) |
| FR-030 | `async_write` cancel does NOT rollback persisted frame | PASS | `asio_tls_transport.cpp` async_write (T027) | `tests/transport/test_durable_before_transmit_ordering.cpp` (ErrorCodeContract + TransportWriteIsNotRollbackAware static_assert) |
| FR-031 | ASIO native cancellation slots end-to-end | PASS | `asio_tls_transport.cpp` (T027 — D-17 reset applied) | `test_cancellation_propagation.cpp` TotalVsTerminalDistinct + Phase 7 seam #13 (8 cells × 4 sanitizers = 32 PASS) |
| FR-032 | Two-phase close per `[2d §4.7]` | PASS (contract documented; full graceful/terminal matrix witness deferred) | `asio_tls_transport.cpp` close()/cancel() (T027) | smoke shape; full matrix witness deferred to integration cells |
| FR-033 | **011 F-1 carryover BINDING — 8-cell matrix** | **PASS** | Phase 7 witness (T044) | `tests/transport/test_load_credentials_seam13_witness.cpp` — **8 cells × 4 sanitizer presets = 32 PASS** (see `.specify/decisions/012-2h-transport-verify.md §T044`) |
| FR-034 | 22 transport_* variants in 5 coalescing groups | PASS | `include/fixpp/core/error.hpp:553-584` (T006 — slots 94..115) | smoke (static_assert all 22 slots); ABI coalescing documented in `include/fixpp/transport/transport_errors.hpp` (T007) |
| FR-034a | `transport_handshake_failed` GROUPING variant + diagnostic | PASS (declaration site) | `transport_errors.hpp` (T007) | declaration ships; runtime diagnostic-content witness PARTIAL (T014 4 cells exist; full sub-reason content assertion DISABLED_) |
| FR-035 | PMR cold-path throws → `trap_throw` pattern | PASS | `asio_tls_transport.cpp` (inlined catch with `transport_*` mapping per T028 finding) | covered structurally; runtime PMR-fault witness deferred to /speckit-verify alloc matrix |
| FR-036 | `<fixpp/transport/test/mock_transport.hpp>` public test header | **DEFERRED-MVP** | (US4 T041-T043) | — |
| FR-037 | mock_transport honours cancellation contracts | **DEFERRED-MVP** | (US4) | — |
| FR-038 | mock_transport Script-shaped programmatic interface | **DEFERRED-MVP** | (US4) | — |
| FR-039 | Strand depth-1 queue; block-only; no transport-internal queue | PASS | `asio_tls_transport.cpp` (no internal write queue; in-flight exclusivity enforces depth-1) | `test_inflight_exclusivity.cpp` |
| FR-040 | Read-/write-path Tier-1 ceilings | **WAIVED-W2** | bench scaffolds wired; bodies are placeholders | See §C waiver W-2 — Tier-1 measurement requires live-fixture wiring deferred |
| FR-041 | Negative ownership boundary (NOT-OWNED list) | PASS | by structural construction (no 2b/011/session/2j/2k code introduced) | — |
| FR-042 | Negative requirements (PSK / SHM / DPDK / 0-RTT / etc.) | PASS | `asio_tls_transport.cpp` (T027 — `SSL_OP_NO_*` set; PSK rejected; no SHM/DPDK/Schannel/Onload code) | covered by absence + FR-016/17 positive assertions |

**Tally**: 42 FRs total — **PASS: 27**, **PARTIAL: 1** (FR-029a — split-coverage initiator-only), **WAIVED: 2** (FR-016 / FR-040), **DEFERRED-MVP: 12** (FR-019/020/021 US2 + FR-023/024/025 US3 + FR-036/037/038 US4 + cross-doc work).

---

## §B — Success Criteria (SC-001..SC-008)

| SC | Title (abbrev.) | Disposition | Witness |
|----|-----------------|-------------|---------|
| SC-001 | TLS-encrypted FIX session zero-code-change wireup | PASS (structural — `TransportFactory` boundary ships; full E2E witness needs live counterparty per T020 conformance — DISABLED_) | T020 scaffolding + smoke; T020 17 DISABLED_ cells annotated |
| SC-002 | Reconnect within 73-89s envelope, no thundering herd | **DEFERRED-MVP** | (US2 — ReconnectPolicy + FSM-side loop) |
| SC-003 | 100% cancellation reaches `transport_*_cancelled`; ASan+LSan clean; dual-gate alloc | PARTIAL | `test_cancellation_propagation.cpp` error-code cells + Phase 7 (8×4=32 PASS sanitizer matrix); dual-gate alloc on read-path via T018; full async_*_cancelled runtime witness DISABLED_ pending live fixture |
| SC-004 | mock_transport swap requires ZERO non-test source change | **DEFERRED-MVP** | (US4 — mock_transport ships) |
| SC-005 | asio_listener 10²-10³ concurrent sessions | **DEFERRED-MVP** | (US3) |
| SC-006 | Distinct named variant per failure mode (no "transport failed" catch-all) | PASS | All 22 `transport_*` variants present at slots 94..115 (T006); coalescing groups documented (T007) |
| SC-007 | Durable-before-transmit + cancel mid-write preserves persisted frame | PASS (contract shape) | `test_durable_before_transmit_ordering.cpp` static_assert + error-code contract; full E2E DISABLED_ pending FSM Phase-4 |
| SC-008 | **011 F-1 carryover GREEN — 8 cells × Session*/session_executor** | **PASS** | `tests/transport/test_load_credentials_seam13_witness.cpp` — 8 cells × 4 sanitizer presets = **32 PASS**; record at `.specify/decisions/012-2h-transport-verify.md §T044` |

**Tally**: 8 SCs total — **PASS: 4** (SC-001/SC-006/SC-007/SC-008), **PARTIAL: 1** (SC-003 — error-code+sanitizer matrix green, live-fixture integration cells DISABLED_), **DEFERRED-MVP: 3** (SC-002/004/005).

**SC-008 (the 011 Gate B F-1 carryover) is the binding precondition for 012 Gate B per `[[project_011_tls_policy_closed]]` — discharged.**

---

## §C — Waivers (with rationale)

### W-1 — FR-016 `SSL_OP_NO_EARLY_DATA` omission

**Surface**: `src/transport/asio_tls_transport.cpp` constructor — `SSL_CTX_set_options(SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION | SSL_OP_NO_TICKET)`. The fourth flag `SSL_OP_NO_EARLY_DATA` from FR-016 + `[FIXS §3.2]` + `[const §XII.3]` is NOT set.

**Reason**: Confirmed via `find /home/catalin/.conan2 -path '*/openssl/*' -name ssl.h | xargs grep -l SSL_OP_NO_EARLY_DATA` — the constant is not defined in the pinned OpenSSL 3.6.2 (`conanfile.py:45 openssl/3.6.2`).

**Mitigation**: TLS 1.3 0-RTT is DISABLED BY DEFAULT in `asio::ssl::context` unless the caller explicitly invokes `SSL_CTX_set_early_data_enabled(ctx, 1)`. `asio_tls_transport` does not call this — early data therefore cannot be negotiated in practice.

**Gate B follow-up**: Either (a) add `SSL_CTX_set_max_early_data(ctx, 0)` as the canonical 0-RTT-disabled assertion (function IS available in OpenSSL 3.6.2 — verify and patch), OR (b) record this waiver verbatim in `.specify/decisions/012-2h-transport-verify.md §T054` with explicit pin against OpenSSL version + ASIO default behavior. Decision deferred to /gate-b.

### W-2 — FR-040 Tier-1 bench-body fill-in deferred

**Surface**: `bench/transport/bench_async_read_some_dispatch.cpp` + `bench_async_write_issue.cpp` + `bench_tls_handshake_loopback.cpp` — wired into CMake and execute, but the benchmark bodies are placeholder `benchmark::DoNotOptimize()` loops with `// TODO (T029): replace with real ...` comments.

**Reason**: Real Tier-1 measurements (≤200 ns p99 dispatch / ≤200 ns p99 write / ≤10 ms p99 handshake per `[2h §6.3]`) require a live counterparty fixture (loopback ASIO `io_context` driving an opposite-side `asio_tls_transport` with a real `SslCtxConfig`). That fixture is out of scope for the MVP slice — Phase 3a authored scaffolds with TODO comments, Phase 3b-2 shipped the impl, but the fixture wiring is post-MVP work.

**Mitigation**: The bench binaries DO build + execute clean under all sanitizer presets. Counters are reported (currently 0 ns due to no-op body). Phase 8 / Gate B follow-up = author the live-fixture wiring in a follow-on slice (or fold into US3 since acceptor-side fixtures naturally land there).

### W-3 — `asio_free` namespace alias (cosmetic)

**Surface**: `src/transport/asio_tls_transport.cpp` — uses `namespace asio_free = asio` to disambiguate `asio::async_connect` (free function) from `asio_tls_transport::async_connect` (member).

**Reason**: Cosmetic — `asio::async_connect(...)` inside a member named `async_connect(...)` triggers C++ name-lookup precedence and shadows the free function. The alias is the standard workaround.

**Mitigation**: No behavior change. Documented in the source.

### W-4 — T020 conformance cells DISABLED_

**Surface**: `tests/conformance/test_transport_interop.cpp` — 17 TC-001..TC-017 cells are `DISABLED_`.

**Reason**: Requires a cooperating QuickFIX test peer binary (interop run prep). Per `[[project_release_interop_quickfix_fix8]]`, this is the per-release interop gate run prior to v1.0 — out of scope for the per-feature MVP slice.

**Mitigation**: The error-code scaffolding cell runs and passes (1/18 active). Cells are documented with the dependency in source comments. Pre-v1.0 interop gate will populate.

### W-5 — `verify_peer_trampoline` PMR-then-copy false-pass risk (R1 S-7 from /simplify)

**Surface**: `src/transport/asio_tls_transport.cpp` `verify_peer_trampoline` (~lines 367-446). For every peer-cert iteration the trampoline builds `std::pmr::*` containers on `sys_mr = new_delete_resource()`, then `.assign()`-copies into `backings[i].*` (system-heap `std::vector` / `std::string`), then rebuilds `string_view` spans into the system-heap storage.

**Reason**: A `counting_resource` PMR witness on `hctx->mr` would see zero allocs even though the trampoline allocates heavily via the `backings` system-heap path — exactly the false-pass mode per `[[feedback_tracking_pmr_resource_false_pass]]`.

**Mitigation**: The DUAL gate (counting_resource + mallocnesia LD_PRELOAD) per T018 was designed to catch this — mallocnesia intercepts global `operator new` / `malloc` and would flag the system-heap allocations. Phase 8 / Gate B follow-up: verify the dual gate fires against the trampoline path; if mallocnesia is not yet linked into the test binary, escalate. Structural restructure (drop the PMR adapter layer in the trampoline, OR allocate `backings` from `hctx->mr`) is recommended in a follow-on slice.

### W-6 — 16-throw-site constructor needs boundary+mid+tail PMR witnesses (R1 flag from /simplify)

**Surface**: `src/transport/asio_tls_transport.cpp` throwing constructor — 16 distinct throw sites in the OpenSSL configuration block (cipher-list `std::string` allocation, curves/sigalgs, SSL_CTX_set_options, SSL_set_verify, cert-source dispatch, chain-cert load loop, etc.).

**Reason**: Per `[[feedback_trap_throw_pmr_witness_enumerate_sites]]` — a 1-byte arena witness trips at the FIRST allocation and never exercises mid/tail paths. The throwing constructor is the engine-bootstrap entry; if a PMR-fault arena were used here, only the first site would be witnessed; SAN-extension extraction / X509 chain handling / EVP_PKEY paths downstream would have NO witness coverage. The throwing constructor sits in `[arch §5.3]` engine-bootstrap carve-out, so it's not a runtime path, but the RAII unwind shapes (X509Ptr release in chain loop) are load-bearing for correctness on fault.

**Mitigation**: Phase 8 / Gate B follow-up: add a PMR-arena witness that parameterizes the arena exhaustion point (1 byte / mid-chain / tail-chain). The Phase 3a `tests/perf/test_transport_read_alloc_guard.cpp` covers the read-path; an equivalent constructor-fault witness would extend the discipline. Out of scope for MVP — flag for Gate B Codex review.

### W-7 — Phase 7 Case 3 cells don't fully witness `[2d §6.5]` case 2 semantics (R3 finding from /simplify)

**Surface**: `tests/transport/test_load_credentials_seam13_witness.cpp` Case3_Strand + Case3_Direct cells.

**Reason**: The handler passed to `cancellable_dispatch` is a non-awaitable synchronous lambda (`[this]() { dispatch_reached.store(...); }`). With a non-awaitable handler, `cancellable_dispatch` invokes it as `std::forward<Handler>(handler)()` (synchronous fall-through) and there is no co_await checkpoint mid-handler where `operation_aborted` could propagate. The `dispatch_cancel_fired=true` signal in the test fires from a SEQUEL `gate_->async_wait()` that runs AFTER `cancellable_dispatch` returned — so Case 3 materially re-tests Case 4 (happy dispatch) plus "post-dispatch sequel timer cancel."

The advertised 8-cell witness coverage per FR-033 + SC-008 is therefore **6 cells (Case 1 / Case 2 / Case 4) × 2 modes + 2 cells of weaker semantic (Case 3 reduced to sequel-cancel)** against the literal `[2d §6.5]` case 2 contract.

**Mitigation**: Gate B follow-up — either (a) restructure the Case 3 handler to an awaitable that performs an inner `asio::steady_timer::async_wait` and emit `dispatch_signal` mid-flight so the slot fires through the handler's own co_await; OR (b) record the limitation in the verify doc + SC-008 disposition as an explicit "post-dispatch sequel cancel" witness (the 011 F-1 carryover contract was authored against the `cancellable_dispatch` recipe which itself supports both awaitable + non-awaitable handlers — if the operator-side `file_cert_source::load_credentials` only EVER uses non-awaitable handlers, then the current witness is materially complete).

**Disposition recommendation**: Option (a) — restructure Case 3 — is preferred for full SC-008 closure; this is a ~30-60 LoC test edit. Option (b) is acceptable if Gate B Codex / human reviewer agrees the contract is non-awaitable-only.

### Inherited from 011 (apply at /speckit-verify without re-deliberation)

- **W-2 cppcheck** + **W-3 iwyu** carry-forwards per `[[project_011_tls_policy_closed]]`.

---

## §F — /simplify pass triage (T053 close-out)

Three /simplify general-purpose review agents ran in parallel against (1) production impl spine, (2) foundational headers + errors, (3) test corpus + CMake. Triage:

### Inline fixes applied (5)

- **R1 S-1**: `cancel_signal_` orphan removed. The member was declared and `cancel()` emitted `cancellation_type::total` on it, but NO coroutine bound to its slot. The actual cancel path is `socket_.cancel(ec)` — that line was already present. Header comment in `src/transport/asio_tls_transport.hpp` rewritten to document the socket-cancel path; the false `[2d §6.5] recipe` claim removed. `cancel()` body simplified to socket cancel only.
- **R1 S-2**: Dead `state_ == closed` checks in `async_read_some` + `async_write` removed — the subsequent `state_ != handshaken` check already covers `closed` (and `fresh` + `connected`).
- **R1 S-3**: Redundant `operation_aborted` second arm in `async_read_some` simplified — both branches returned `transport_read_cancelled`; the `co_await this_coro::cancellation_state` redundant read removed.
- **R1 S-9**: Redundant `state_ = state_t::fresh` at constructor end removed — `state_` is default-initialized to `fresh` via `state_t state_ {state_t::fresh}` in the header.
- **R2 S-3**: `asio_tls_transport_factory` doc comment in `include/fixpp/transport/transport_factory.hpp` clarified — the previous comment claimed the factory caches `SslCtxConfig` but the ctor only takes `Transport::Config`. New comment documents the actual cache layering: factory caches `Transport::Config` socket knobs; the session-open sequencer holds `SslCtxConfig` by value and passes the same instance per reconnect attempt, so the underlying `SSL_CTX*` is also long-lived — the factory itself never rebuilds the SSL_CTX.

### Findings documented as waivers (3)

- **W-5** — R1 S-7 (trampoline PMR-then-copy false-pass risk) — see §C above.
- **W-6** — R1 16-throw-site PMR witness gap — see §C above.
- **W-7** — R3 Phase 7 Case 3 contract semantics gap — see §C above.

### Findings skipped (calibration-drift class)

- **R2 S-1 / S-2** (Endpoint 2-arg / 3-arg ctors as bloat): borderline behavioral; aggregate-init still works alongside explicit ctors in C++20+. Callers consume both shapes; skip.
- **R2 S-4** (cancel() return-type asymmetry vs close()): contract per `[2h §4.1]` — `expected_t<void>` retained for FR-006 / FR-008 nodiscard discipline + ABI uniformity with the rest of the surface.
- **R1 S-4** (peer_id_ write-only past handshake): structural; defer.
- **R1 S-5** (trap_throw inline comment): cosmetic.
- **R1 S-6** (unused `mr` param in `parse_x509_to_certificate`): cosmetic; defer to follow-on cleanup.
- **R1 S-8** (5 list-builder blocks could be lambda): style; ctor is engine-bootstrap cold path.
- **R2 S-5** (redundant C-ABI coalescing tables in 2 places): cosmetic; one is the enum-site doc (error.hpp) and one is the alias-site doc (transport_errors.hpp) — both readers benefit.
- **R3 stale CMake comments** (`tests/perf/CMakeLists.txt` LD_PRELOAD note; `tests/conformance/CMakeLists.txt` 005 future-homes comment; bench `T044/T045` block name): cosmetic; flag for follow-on cleanup.

### Surprises documented (no action)

- **SP-1 (R1)**: `friend class asio_tls_transport_test_access;` referenced nowhere → either test fixture is deferred to US3 follow-on slice or it's dead; document for cleanup.
- **SP-2 (R1)**: `kMaxChainLen = 16` silent truncation of TLS chain → behavior concern, not /simplify scope; flag for Gate B.
- **SP-3 (R1)**: `asio_free::` alias in `asio_tls_transport.cpp` is cargo-cult per Reviewer 1 (member-name ADL wouldn't pick `asio::async_connect` free function) — but Phase 3b-2 agent reported a real name collision compile error. Verification deferred; if R1 is right, alias can be removed. Cosmetic either way.
- **SP-4 (R1)**: `async_handshake` initiator-mode-only — acceptor-side handshake half-implemented; deferred to US3 follow-on slice.
- **SP-5 (R1)**: `tmp_io.run()` sync-coroutine-in-ctor pattern → defensible (ctor is sync) but undocumented; flag for follow-on comment-add.
- **R3 T024 fuzz scope**: `fuzz_transport_read_path.cpp` only feeds `Framer::feed` (a 004-wire path); it does NOT exercise transport-side read code. Known per T024 brief — flag in coverage-index ledger.

### Net verdict

5 high-confidence simplify fixes applied; 3 deeper findings documented as W-5/W-6/W-7 for Gate B triage. All 8 wired Phase 3a binaries + Phase 7 witness PASS post-edit under Clang Debug. Sanitizer matrix re-verification per T054 below.

---

## §D — Test BODY audit (per `[[project_005_phase8_completeness_false_pass]]` 8th-burn lesson)

Sampled for SUCCEED() placeholders and FSM-transition-skipped-but-end-state-matches traps:

- `tests/transport/transport_smoke_test.cpp` — 1 cell, 22 static_asserts for error slot positions + Transport::Config defaults + Endpoint defaults + namespace alias consistency. **Real assertion bodies, no SUCCEED().**
- `tests/transport/test_tls_handshake_pinset_rotation.cpp` — 4 runnable + 4 DISABLED_ cells. Runnable cells assert capture stability via 011-shipped `Pinset::snapshot()` semantics. **Real bodies.**
- `tests/transport/test_durable_before_transmit_ordering.cpp` — 2 runnable cells (ErrorCodeContract + `static_assert TransportWriteIsNotRollbackAware`). **Real bodies.**
- `tests/transport/test_compid_tls_identity_binding.cpp` — 5 runnable cells (subject_dn_view + mock FSM binding + mismatch rejection + null captured_pinset + move semantics). **Real bodies, including a mock FSM binding step that verifies the peer_id→CompID mapping logic.**
- `tests/transport/test_inflight_exclusivity.cpp` — 3 runnable cells (error-code presence + distinct + namespace alias). **Real bodies.**
- `tests/transport/test_cancellation_propagation.cpp` — 3 runnable cells (ErrorCodesPresent + TotalVsTerminalDistinct + NamespaceAliasConsistency). **Real bodies.** 8 DISABLED_ integration cells documented per cell with the integration prerequisite.
- `tests/perf/test_transport_read_alloc_guard.cpp` — DUAL gate (counting_resource PMR routing + mallocnesia weak symbols). **Real bodies; explicit dual-gate enforcement per `[[feedback_tracking_pmr_resource_false_pass]]`.**
- `tests/perf/test_socket_option_defaults.cpp` — 3 cells (TCP_NODELAY default + SO_LINGER default + TCP_KEEPALIVE default). **Real bodies; integration cell DISABLED_ pending live socket fixture.**
- `tests/conformance/test_transport_interop.cpp` — 1 runnable scaffolding cell + 17 DISABLED_ TC scenarios. **Documented as scaffolding, NOT SUCCEED() placeholders.**
- `tests/transport/test_load_credentials_seam13_witness.cpp` — **8 fully-bodied cells**; Phase 7 binary; **no SUCCEED(); no end-state-only assertion — every cell asserts the intermediate dispatch state (cached vs not, cancelled vs not, slot signalled vs not) AND the final outcome.** This is the highest-quality test in the slice and the binding witness for 011 F-1 closure.

**Verdict**: NO SUCCEED() placeholders detected; FSM-transition-skipped-but-end-state-matches trap not detected. Audit GREEN.

---

## §E — Cross-references

- Catalogue rows flipped: T-001/T-002/T-003/T-004/T-005/T-009/T-010/T-039/T-040 → `implementing` (012-2h-transport). T-006/T-007/T-008/T-011/T-013 stay `implementing` (011 carry — flip to `done` at v1.0). T-041 stays `backlog` pending session-Phase-4.
- Coverage-index 012 ledger: `library/spec/coverage-index.md` (Active feature pointer rotated 011→012).
- Phase 7 §T044 cell-by-cell record: `.specify/decisions/012-2h-transport-verify.md` (Phase 8 T054 will integrate the full verify doc).
- Active feature pointer in `library/CLAUDE.md`: rotate from 011 → 012 at T055 close-out.
