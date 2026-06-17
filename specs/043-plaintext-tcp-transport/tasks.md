---
description: "Task list for 043-plaintext-tcp-transport implementation"
---

# Tasks: Plaintext TCP transport (insecure_plain_tcp)

**Input**: Design documents from `specs/043-plaintext-tcp-transport/`
**Prerequisites**: plan.md ✅, spec.md ✅ (US1/US2 P1, US3 P2), research.md (D-1..D-13) ✅,
data-model.md (E-1..E-7) ✅, contracts/ ✅. Gate A CONVERGED (round 3, user-signed-off 2026-06-17).

**Tests**: INCLUDED and TDD-ordered (write RED first, then GREEN) — mandatory per Constitution Art. VII
and the project pipeline. Every Success Criterion (SC-001..SC-008) has a named witness task below.

**Repository root** = the library submodule
`research/G19-fix-fpml-iso20022/library/`. All paths below are relative to it.

**Organization**: by user story so each is independently implementable + testable. US3 (P2) builds on the
effective-factory resolution + FSM wiring established in US1 (P1); this dependency is called out explicitly
below (it is intrinsic to the open() validation block, not an organizational artifact).

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different files, no incomplete-task dependency)
- **[Story]**: US1 / US2 / US3 (Setup / Foundational / Polish carry no story label)

---

## Phase 1: Setup (Shared baseline)

**Purpose**: Establish a green pre-feature baseline so SC-006 (zero TLS-path regression) is measurable.

- [X] T001 Confirm a clean green baseline on branch `043-plaintext-tcp-transport`: build + full `ctest`
  (debug) pass, record the pre-feature test count for the SC-006 regression check, and `codegraph sync`
  from the submodule. No code change — gate the work on a known-green starting point.
  **Baseline (2026-06-17): debug build green (175/175), `ctest` 466 tests all pass (exit 0, 96.7s);
  codegraph index fresh (709 files). SC-006/T029 regress against ≥466 debug tests.**

---

## Phase 2: Foundational (Blocking prerequisites)

**Purpose**: Leaf-level type/vtable scaffolding every user story references. These are enum/virtual-surface
additions whose "test" is *everything still compiles and the existing suite stays green* (SC-006); their
behaviour is exercised by the story tests in Phase 3+.

**⚠️ CRITICAL**: No user-story work begins until this phase is complete.

- [X] T002 Add `enum class transport_security_kind : std::uint8_t { tls, plaintext };` and the **defaulted**
  (non-pure) virtual `[[nodiscard]] virtual transport_security_kind kind() const noexcept { return
  transport_security_kind::tls; }` to the abstract `TransportFactory` in
  `include/fixpp/transport/transport_factory.hpp` (D-5, E-3). Pure-virtual count MUST stay **3**
  (`[const §XIV.2]`); defaulted-default `tls` keeps the ~11 `tests/session/` factory doubles compiling.
- [X] T003 [P] Override `kind()` → `transport_security_kind::tls` explicitly on `asio_tls_transport_factory`
  in `src/transport/transport_factory.cpp` (+ decl) (D-5). (Depends on T002.)
- [X] T004 [P] Add the **bare** closed-enum value `SecurityProfile::kind::insecure_plain_tcp = 4` (NO
  attribute yet — the `[[deprecated]]` friction is US2/T019) to `include/fixpp/session/security_profile.hpp`
  (E-1, FR-001). `unset (0)` reject unchanged (FR-007).

**Checkpoint**: factory `kind()` query + the new profile enumerator exist; suite green.

---

## Phase 3: User Story 1 — Establish a plaintext FIX session over plain TCP (Priority: P1) 🎯 MVP

**Goal**: A plaintext initiator and acceptor exchange Logon → app → Logout over a plain TCP socket with no
TLS handshake and no TLS bytes; auto-derived plaintext factory; FSM + acceptor handshake skip;
`live_peer_id_` fail-closed-`nullopt`.

**Independent Test**: Stand up a plaintext initiator + acceptor on loopback (and direct-drive the transport
against a loopback `asio::ip::tcp::acceptor`); assert a Logon/Logout round-trip completes over a plain
socket and that no TLS ClientHello is ever emitted (SC-001).

### Tests for User Story 1 (write FIRST, ensure they FAIL) ⚠️

- [X] T005 [P] [US1] Direct-drive `asio_plain_transport` loopback round-trip test (connect → read_some →
  composed write → idempotent close; cancellation → `transport_*_cancelled`; post-close → `*_already_closed`;
  assert no TLS ClientHello byte) in `tests/transport/test_asio_plain_transport.cpp` (NEW) — SC-001 (transport).
- [X] T006 [P] [US1] `Transport::Config` knob witness + close-path witness: a non-default TCP knob
  (e.g. `tcp_nodelay`/keepalive/buffer) is observable on the plain socket, and `close()` on an established
  plain transport returns with **no** `tls_close_timeout`-length delay and emits **no** TLS close-notify, in
  `tests/transport/test_asio_plain_transport_config.cpp` (NEW) — SC-008 (FR-010/FR-011).
- [X] T007 [P] [US1] End-to-end plaintext session: a plaintext **acceptor driven through
  `run_accept_loop` on `insecure_plain_tcp`** + a plaintext initiator complete a FIX Logon → Logout over a
  loopback socket (exercises all three E-7 acceptor sites, not just a direct-transport loopback) in
  `tests/session/test_session_plaintext_roundtrip.cpp` (NEW) — SC-001 (acceptor end-to-end).
- [X] T008 [P] [US1] Authorization-inert witness: on a plaintext session
  `compid_authorization_policy.authorize(...)` is **not** called, **no** peer-identity state exists
  (`live_peer_id_ == nullopt`) on both the reconnected and accepted handoffs, while `check_comp_id` still
  **rejects** a mismatched inbound 49/56 CompID, in `tests/session/test_session_plaintext_authz.cpp` (NEW)
  — SC-004 (FR-008a, D-10). **FR-009 NOTE (corrected post-implement, user-ratified 2026-06-17):** the
  original amendment here assumed `EncryptMethod(98)≠0` was already rejected inbound and "not modified by
  043". That was FALSE — the pre-043 baseline skipped tag 98 inbound (`interpret_logon`; S-021/TC-017 gap),
  so plan.md's §XII.7 Constitution-Check row was a false-green. Per the user decision, 043 now **enforces**
  the inbound reject (new task **T030**); FR-009 is witnessed by the dedicated all-profiles witness
  `tests/session/test_interpret_logon_encrypt_method.cpp` (NOT a plaintext-only cell here — interpret_logon
  is profile-agnostic, so the dedicated witness is stronger). T008 cell-4 is therefore satisfied by T030's witness.

### Implementation for User Story 1

- [X] T009 [US1] Create `asio_plain_transport.{hpp,cpp}` in `src/transport/` — mirror `asio_tls_transport`
  with TLS stripped: keep `socket_`/`exec_`/`cfg_`/in-flight flags/`apply_socket_options_`/connect-timeout;
  drop `ssl_ctx_`/`ssl_stream_`/`captured_pinset_`/`peer_id_`/`role_`; state `{fresh,connected,closed}` (no
  `handshaken`); `async_read_some`/`async_write` on `socket_`; `close()` = `socket_.close()` with **no**
  `SSL_shutdown` and no `tls_close_timeout` wait (D-1, D-2, E-2; FR-002/FR-011). Makes T005/T006 pass.
- [X] T010 [US1] Add `asio_plain_transport_factory final : public TransportFactory` (decl in
  `include/fixpp/transport/transport_factory.hpp`, body in `src/transport/transport_factory.cpp`) +
  `make_asio_plain_transport_factory(Transport::Config) noexcept` (NO `SslCtxConfig` arg): `make()` ignores
  `ssl_cfg`, mints via `trap_throw` → `transport_factory_failed`; `reload_credentials()` →
  `error::session_invalid_argument`; `cert_source_snapshot()` → `nullptr`; `kind()` override → `plaintext`;
  **concrete** (non-virtual) `make_accepted()` adopting an accepted plain socket (D-3, E-4; FR-003/FR-004).
- [X] T011 [US1] `ReconnectFsm` (`include/fixpp/session/reconnect_fsm.hpp` + `src/session/reconnect_fsm.cpp`):
  add `is_plaintext_` + `set_plaintext_profile(bool)` and the new `set_transport_factory(TransportFactory*)
  noexcept` setter (mirroring `set_tls_profile`); step 6 (~`:256-272`) skips the
  `dynamic_cast<TlsTransport*>` + `async_handshake` when `is_plaintext_` (connect → Logon), and skips step 7
  authz (no `hr`); the non-plaintext null-cast→error path stays fail-closed (D-7, D-4, E-5; FR-005). Update
  the `:238` "owned by `transport_factory_override`" comment to "owned by the Session
  (override or `effective_transport_factory_`)".
- [X] T012 [US1] `Session::open()` initiator wiring in `src/session/session.cpp` (~`:875-920` + `:1161-1179`):
  add the Session-owned `std::shared_ptr<TransportFactory> effective_transport_factory_` member; resolve the
  **effective** factory once (plaintext + no override ⇒ built-in plaintext factory auto-derive; else
  `override.value_or(engine_default)`); for `insecure_plain_tcp` the SK→TK mapping leaves
  `tls_profile=unset` and builds **no** `SslCtxConfig`; call `reconnect_fsm_.set_plaintext_profile(true)` +
  `set_transport_factory(effective_transport_factory_.get())` **BEFORE** any `drive_reconnect()` (alongside
  `set_tls_profile`, `:1178`) (D-4 initiator, D-6 steps 2 & 4, D-7 mapping, E-6). (FR-003a.) The consistency
  **reject** arm is US3/T023.
- [X] T013 [US1] Enforce the `live_peer_id_`-stays-`nullopt` **MUST** on the plaintext handoffs in
  `src/session/session.cpp`: `install_reconnected_transport` (~`:405`) and `attach_accepted_transport`
  (~`:534`) MUST NOT construct/pass a fake `handshake_result`; pass a default `hr{}` and leave
  `live_peer_id_ == nullopt` for `insecure_plain_tcp` (D-10 #2/#3). Makes T008 pass.
- [X] T014 [US1] Acceptor sites #1 + #3 in `src/session/engine.cpp` `run_accept_loop`: add an explicit
  `insecure_plain_tcp` arm to the profile→`ssl_cfg` map (~`:664-681`) that builds **no** `ssl_cfg` and does
  **not** fall through to `mtls_ca`; and skip the post-accept `dynamic_cast<TlsTransport*>` +
  `async_handshake` block (~`:783-797`), proceeding with a default `hr{}` instead of `close(); continue;`
  (D-4 site #1, D-8 site #3, E-7).
- [X] T015 [US1] Acceptor site #2 (listener Config contract) in `src/transport/asio_listener.{hpp,cpp}`
  (+ `run_accept_loop` in `engine.cpp`): add `transport_security_kind transport_kind{tls}` to
  `asio_listener::Config`; `async_accept()` mints `make_asio_plain_transport_factory` when
  `transport_kind == plaintext`, holding the accept factory **concretely-typed** (TLS member + plain member,
  or a `variant<shared_ptr<asio_tls_transport_factory>, shared_ptr<asio_plain_transport_factory>>` — NOT a
  base `TransportFactory*`, since `make_accepted()` is concrete-only); `run_accept_loop` sets
  `lcfg.transport_kind` and leaves `lcfg.ssl_cfg` default for plaintext (D-4 site #2, E-7; FR-004). Makes
  T007 pass. (Coordinates with T014 — both touch `run_accept_loop`; do not parallelize.)
- [X] T016 [US1] Close the strand-confinement assert hole: extend `assert_transport_on_session_strand`
  (`src/session/engine.cpp` ~`:317-332`, `#ifndef NDEBUG`) with an `asio_plain_transport*` arm (or promote
  `socket_executor()` to a base `Transport` accessor) so the R8 invariant is checked for plaintext
  accepted/reconnected transports, not silently skipped on the null TLS downcast (D-13 P3-A). **In the same
  edit, fix the stale `engine.cpp:322` comment** ("The engine exclusively uses asio_tls_transport") — it
  sits inside this function body, so reconcile it here (dated note, D-13) rather than in a separate task to
  avoid a same-region edit collision.

**Checkpoint**: US1 fully functional — a plaintext session round-trips both roles; T005–T008 GREEN.

### Added post-Gate-A (US1-d discovery — user-ratified 2026-06-17)

- [X] T030 [const §XII.7] inbound `EncryptMethod(98)≠0` reject (closes the pre-existing inbound gap that
  made plan.md's §XII.7 Constitution-Check row a false-green; S-021 "inbound 98≠0 not handled" / TC-017).
  `interpret_logon` (`src/session/admin_messages.cpp`) now scans tag 98 (was skipped) and rejects a Logon
  with `98 ≠ "0"` — present-but-malformed fails closed — via the existing `session_invalid_logon` (no new
  error slot). **Unconditional / all-profiles** (interpret_logon is profile-agnostic). Witness
  `tests/session/test_interpret_logon_encrypt_method.cpp` (4 cells, mutation-proven discriminating). Zero
  regression (471 debug ctest). spec.md FR-009 + plan.md §XII.7 row + the Gate A record corrected to retire
  the false-green. Satisfies T008 cell-4 / FR-009.

---

## Phase 4: User Story 2 — Opt-in only, with loud insecure friction (Priority: P1)

**Goal**: Selecting `insecure_plain_tcp` is a loud compile-time `[[deprecated]]`-class diagnostic; the
no-implicit-default rule is preserved (`unset` still rejected; never selected silently).

**Independent Test**: Compile a TU selecting `insecure_plain_tcp` and assert the deprecation-class diagnostic
fires; assert a default-constructed `SecurityProfile` (kind::unset) is still rejected at `open()`.

### Tests for User Story 2 (write FIRST) ⚠️

- [X] T017 [P] [US2] Negative-compile witness (SC-005): a one-TU program selecting
  `kind::insecure_plain_tcp` compiled with `-Werror=deprecated-declarations` MUST **fail compilation**, via
  the repo's **automated** negative-compile / CMake `try_compile` harness (NOT a runtime gtest). SC-005
  requires the friction be *observable at build time* — the research.md "OR a documented manual witness"
  fallback is **excluded**: the witness MUST be automatable/repeatable in CI, not a one-time manual note.
- [X] T018 [P] [US2] No-implicit-default witnesses (SC-002): a default-constructed / `unset`
  `SecurityProfile` is still rejected at `Session::open()` with `error::invalid_session_config`, and no path
  selects `insecure_plain_tcp` implicitly (US2 AC2/AC3) — extend
  `tests/session/test_session_open_rejects_unset_security_profile.cpp` or add a sibling.

### Implementation for User Story 2

- [X] T019 [US2] Add the `[[deprecated("insecure_plain_tcp disables transport security (no
  TLS/encryption/peer-auth); use only over a separately-secured link (colo/VPN) — prefer
  mtls_pinned/mtls_ca")]]` attribute to the `insecure_plain_tcp` enumerator in
  `include/fixpp/session/security_profile.hpp` (D-9, FR-006). Reconcile the adjacent session-stub comment
  ("A future no-TLS / plaintext escape value …") per D-13.
- [X] T020 [US2] Pragma-suppression sweep: wrap **every** fixpp-internal reference to the now-deprecated
  enumerator (the `session.cpp` SK→TK mapping + auto-derive added in US1, any `engine.cpp` reference, and
  fixpp's own tests that select the value) in `#pragma clang diagnostic ignored
  "-Wdeprecated-declarations"` (idiom at `session.cpp:1173-1176`), so fixpp's build stays clean while the
  operator's selection site warns (D-9). Grep-gate: no internal selection of `insecure_plain_tcp` left
  unsuppressed. **(Run after US1's reference sites land — depends on T012/T014.)**

**Checkpoint**: friction is build-observable; no-implicit-default preserved; T017/T018 GREEN; fixpp build clean.

---

## Phase 5: User Story 3 — Fail closed on profile↔transport mismatch at open() (Priority: P2)

**Goal**: A profile↔factory kind disagreement — keyed on the **effective/resolved** factory, not just an
explicit override — is rejected at `Session::open()` with `error::invalid_session_config` before any
connect/handshake; matched pairings (and a plaintext profile with no override) open.

**Independent Test**: Construct each mismatched (profile, factory) pairing — including a TLS profile with NO
override but a plaintext *engine-default* factory — and assert `open()` returns `invalid_session_config`;
construct each matched pairing and assert `open()` succeeds.

> Builds on US1's effective-factory resolution + FSM wiring (T012). US3 adds the **reject** arm and the
> matrix witnesses.

### Tests for User Story 3 (write FIRST) ⚠️

- [X] T021 [P] [US3] Effective-factory mismatch matrix (SC-003, US3 AC1–4) in
  `tests/session/test_session_plaintext_factory_mismatch.cpp` (NEW): reject ⇒ (a) `insecure_plain_tcp` +
  explicit TLS factory override, (b) TLS profile + explicit plaintext factory override, (c) **TLS profile +
  no override + plaintext engine-default factory** (the effective-factory case); open ⇒ plaintext+plaintext,
  TLS+TLS, and plaintext profile with **no** override (auto-derived). Each reject asserts
  `error::invalid_session_config` at `open()`, before any connect.
- [X] T022 [P] [US3] Effective-factory-reaches-mint witness (research.md D-4 test note) in
  `tests/session/`: a plaintext/no-override session **and** a matched TLS/no-override session actually
  connect through the **checked effective factory** (the resolved factory reaches the FSM `make()` mint path
  — not a late nullptr/stale-pointer failure in `drive_reconnect_attempt()`), and the mismatched
  engine-default case fails clean at `open()`, never at the FSM cast.

### Implementation for User Story 3

- [X] T023 [US3] Add the FR-008 consistency **reject** arm to `Session::open()` (`src/session/session.cpp`
  ~`:875-920`, in the effective-factory block from T012): require the **effective** factory's `kind()` to
  match the profile category (`tls` for mtls_ca/mtls_pinned/one_way_ca; `plaintext` for
  `insecure_plain_tcp`); on mismatch `co_return std::unexpected(error::invalid_session_config)` (slot 53)
  **before** any FSM spawn — catching both an explicit mismatched override and a wrong engine-default
  (D-6 step 3, E-6; FR-008). Makes T021/T022 pass.

**Checkpoint**: all three stories independently functional; mismatch fails closed at open().

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Doc reconciliation, operator-facing catalogue/B&L, completeness + regression gates
(Gate-B preconditions).

- [X] T024 [P] D-13 stale-doc reconciliation (dated note, not silent rewrite): correct
  `tls_transport.hpp:69-71` ("every v1.0 transport is TLS-capable") for the plaintext transport's existence.
  (The sibling stale comment at `engine.cpp:322` is reconciled in T016 — same function body — to avoid a
  same-region edit collision; this task is now `[P]`-safe as it touches only `tls_transport.hpp`.)
  **Also reconcile (US1-b drift): `reconnect_fsm.hpp:15-18` + `:114-116` still say `factory_` is "owned by
  `transport_factory_override`" — now it can be owned by the Session's `effective_transport_factory_`
  (T011 updated only `:238`). Do ONE exhaustive grep-sweep for stale "owned by transport_factory_override"
  / "transport is TLS"-class comments, not per-finding patches (`[[feedback_verify_caught_design_pivot_stale_doc_bundle_drift]]`).**
- [X] T025 [P] Add limitation **L-043-x** to `spec/behaviors-and-limitations.md`: `insecure_plain_tcp`
  provides **no peer authentication** (CompID↔identity binding skipped) and plaintext accepted transports
  receive **no** TLS-validation event hooks (the inert `set_listener_events` wiring, E-7/D-10).
- [X] T026 [P] Update `spec/feature-catalogue.md` (new row) + `spec/coverage-index.md` for 043 (per the
  feature-completeness convention).
- [X] T027 Validate `quickstart.md`: exercise the documented initiator + acceptor + misconfiguration-reject
  flows against the built library; fix any drift.
- [X] T028 Feature completeness audit (Gate-B precondition): assert exact-set traceability
  tasks ↔ FR-001..FR-013 ↔ SC-001..SC-008 ↔ catalogue row — 100% covered or explicitly waived with reason.
- [ ] T029 SC-006 regression + §IX.1 matrix: full suite green vs the T001 baseline (zero TLS-path
  regression); run the sanitizer/coverage matrix (debug/ASan/UBSan/TSan) ONE PRESET AT A TIME, confirm the
  new branches are covered (lcov DA/BRDA basis), then `codegraph sync`. (This is the evidence
  `/speckit-verify` consumes; not the verify gate itself.)

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (P1)** → no deps.
- **Foundational (P2)** → after Setup; **BLOCKS all stories** (T002 before T003; T004 independent).
- **US1 (P3, P1-priority)** → after Foundational. The MVP; largest surface.
- **US2 (P4, P1-priority)** → after Foundational; **T020 depends on US1's reference sites (T012/T014)**;
  T019/T017/T018 are otherwise independent of US1.
- **US3 (P5, P2-priority)** → after Foundational; **T023 extends US1's open() effective-factory block
  (T012)** and **T022 depends on US1's FSM mint wiring (T011/T012)**.
- **Polish (P6)** → after the stories it documents/verifies are complete.

### Within each story

- Tests (T005–T008, T017–T018, T021–T022) are written FIRST and must FAIL before their implementation.
- T009 (transport) before T010 (factory mints it) before T015 (listener uses the factory).
- T011/T012 (FSM + open() wiring) before T013 (handoff nullopt) and before US3's T023.
- T014 and T015 both edit `run_accept_loop` — sequential, not parallel.

### Parallel opportunities

- Foundational: T003 ∥ T004 (different files; both after/independent of T002 as noted).
- US1 tests: T005 ∥ T006 ∥ T007 ∥ T008 (distinct new files).
- US1 impl: T009 and T010 are sequential (T010 needs the transport type); T011 ∥ T009/T010 (FSM header is
  independent until wired). session.cpp/engine.cpp tasks (T012–T016) largely serialize on shared files.
- US2 tests: T017 ∥ T018. US3 tests: T021 ∥ T022.
- Polish: T024 ∥ T025 ∥ T026.

---

## Implementation Strategy

### MVP (US1 only)

1. Setup (T001) → Foundational (T002–T004) → US1 (T005–T016).
2. **STOP and VALIDATE**: plaintext round-trip both roles, no TLS bytes, authz inert, `live_peer_id_`
   `nullopt` (SC-001, SC-004, SC-008). This is the production-interop + benchmark-fairness capability
   (SC-007 becomes satisfiable).

### Incremental delivery

1. Foundation ready.
2. + US1 → plaintext session works (MVP).
3. + US2 → loud opt-in friction + no-implicit-default guardrail (the security-surface guardrail).
4. + US3 → fail-closed boundary on profile↔factory mismatch.
5. Polish → docs, catalogue/B&L, completeness audit, regression + coverage matrix.

### Notes

- Surgical (`[const §XIV.2]`, FR-013): exactly 1 new transport + 1 new factory + 1 enum value + 1
  factory-kind enum + the defaulted `kind()` + profile-gated branches. No new wire field, error slot,
  config field, or codegen surface.
- Next pipeline step after this file: `/speckit-analyze` (step 6), then `/speckit-checklist` + audit
  (steps 7/9), then `/speckit-implement`.

---

## T028: Feature Completeness Audit (Gate-B precondition) — 2026-06-17

**Exact-set traceability: FR-001..FR-013 × SC-001..SC-008 × tasks × witnesses**

All FRs and SCs must map to ≥1 implementing task + ≥1 witness, OR be explicitly dispositioned.

### FR traceability

| FR | Description (summary) | Implementing task(s) | Witness(es) | Status |
|---|---|---|---|---|
| FR-001 | `insecure_plain_tcp` kind added to `SecurityProfile` | T004, T019 | T018 (`unset` still rejected / no-implicit); T017 (deprecated-compile fires) | GREEN |
| FR-002 | `asio_plain_transport` — plain TCP, 5 base ops, no TLS/handshake | T009 | T005 (loopback round-trip, no ClientHello), T006 (close path) | GREEN |
| FR-003 | Plaintext factory mints `asio_plain_transport`, credential-free | T010 | T005, T007 | GREEN |
| FR-003a | Auto-derive plaintext factory from profile (no override needed) | T012 | T022 (effective-factory-reaches-mint cell), T021 (auto-derive open cell) | GREEN |
| FR-004 | Acceptor `make_accepted()` path symmetry | T010, T015 | T007 (end-to-end acceptor via `run_accept_loop`) | GREEN |
| FR-005 | Reconnect FSM skips handshake for `insecure_plain_tcp`; fail-closed for TLS | T011, T012 | T005 (no TLS bytes), T007 (Logon reached directly after connect) | GREEN |
| FR-006 | `[[deprecated]]` friction on `insecure_plain_tcp` selection site | T019, T020 | T017 (negative-compile `try_compile` harness, RED on selection) | GREEN |
| FR-007 | No-implicit-default — `unset` still rejected; `insecure_plain_tcp` never selected silently | T002, T004, T020 | T018 (`unset` rejected, no-implicit-default) | GREEN |
| FR-008 | Fail-closed consistency: effective factory kind must match profile at `open()` | T023 | T021 (all four mismatch + match cells), T022 (engine-default mismatch fails at `open()`) | GREEN |
| FR-008a | `compid_authorization_policy` skipped for plaintext; `check_comp_id` still active | T013 | T008 (auth-inert + `live_peer_id_==nullopt` + `check_comp_id` rejects mismatch) | GREEN |
| FR-009 | Inbound `EncryptMethod(98)≠0` rejected unconditionally / all profiles | T030 | `tests/session/test_interpret_logon_encrypt_method.cpp` (4 cells, mutation-tested: absent/zero-valid/nonzero-reject/malformed-closes) | GREEN |
| FR-010 | TCP knobs honoured on plaintext path | T009 | T006 (non-default TCP knob observable + no `tls_close_timeout`) | GREEN |
| FR-011 | `close()` plain TCP — no TLS bidi shutdown / no `tls_close_timeout` | T009 | T006 (close returns without timeout delay, no close-notify) | GREEN |
| FR-012 | Scope exclusion: NO bench driver shipped | — | Absence check: no new bench file; `benchmark-plan.md` rows remain satisfiable but unrun (T001 baseline + T029 regression) | GREEN (MUST-NOT, verified by absence) |
| FR-013 | No new public wire fields / error slots / codegen surface | — | Absence check: `git diff` introduces no new `error::*` enumerators, no codegen schema changes, no new proto/wire fields; new `transport_security_kind` enum + `insecure_plain_tcp` are the explicitly-permitted new surface per spec | GREEN (MUST-NOT, verified by absence) |

### SC traceability

| SC | Description (summary) | Witness task(s) | Test file(s) | Status |
|---|---|---|---|---|
| SC-001 | Plaintext Logon/Logout round-trip both roles, no TLS ClientHello | T005, T007 | `test_asio_plain_transport.cpp` (no-TLS bytes), `test_session_plaintext_roundtrip.cpp` (end-to-end via `run_accept_loop`) | GREEN |
| SC-002 | `unset` still rejected; no implicit default | T018 | `test_session_open_rejects_unset_security_profile.cpp` | GREEN |
| SC-003 | All profile↔factory mismatch directions rejected at `open()`; matched + auto-derive open | T021, T022 | `test_session_plaintext_factory_mismatch.cpp` | GREEN |
| SC-004 | Cert-identity auth skipped; `live_peer_id_==nullopt`; `check_comp_id` still active | T008 | `test_session_plaintext_authz.cpp` | GREEN |
| SC-005 | `insecure_plain_tcp` selection fires deprecation-class diagnostic (automated negative-compile) | T017 | `test_insecure_plain_tcp_deprecated.cpp` (CMake `try_compile` harness) | GREEN |
| SC-006 | Zero TLS-path regression vs pre-feature baseline | T029 (orchestrator) | Full suite run (T029, handled by orchestrator `/speckit-verify`); `ctest` stays ≥466 + all prior tests green | PENDING T029 |
| SC-007 | `benchmark-plan.md` `TLS off` rows become satisfiable | — | Enablement: the plaintext transport exists; bench driver is out of scope (FR-012). SC-007 is satisfied transitively by SC-001 (a plaintext session can be stood up over a real socket) | GREEN (enablement, no direct witness needed) |
| SC-008 | TCP knob effective + `close()` no `tls_close_timeout` | T006 | `test_asio_plain_transport_config.cpp` | GREEN |

### Catalogue row

**T-042** added to `spec/feature-catalogue.md` Transport section (after T-041). S-021 amended (inbound 98≠0 now handled via T030). `spec/coverage-index.md` §4.3.1 and §4.3.3 rows updated. Feature ledger section added.

### Summary

- **13/13 FRs covered** (FR-012 and FR-013 are MUST-NOTs verified by absence; FR-009 closed via T030 post-Gate-A; all others have positive witnesses).
- **7/8 SCs GREEN** (SC-006 pending T029 orchestrator run; all others green with direct witnesses).
- **0 unexplained gaps** — every FR/SC is either positively witnessed or explicitly dispositioned.
- **SC-006 disposition**: pending T029 (sanitizer/coverage matrix), handled by the orchestrator at `/speckit-verify`. Pre-feature debug baseline was 466 tests; post-implementation debug count is 474 (8 new test binaries). The 8 additions + 0 regressions confirm additive-only behavior. T029 provides the formal matrix evidence.
