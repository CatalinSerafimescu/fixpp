# Behaviors & Limitations — CLOSED / RESOLVED archive

Entries relocated out of [`behaviors-and-limitations.md`](./behaviors-and-limitations.md) on 2026-07-08
because each **self-declares** its behavior/limitation as RESOLVED / SUPERSEDED / LIFTED / DISCHARGED /
CLOSED by a later feature — i.e. the limitation no longer exists in shipped code. They are preserved
here (not deleted) as historical record; their IDs remain citable and are still referenced by ID from
the live file and from CLAUDE.md / phase-4 docs / memory. Nothing here describes current behavior.

Cut rule (user decision 2026-07-08): move ONLY self-declared-resolved entries. `wontfix` / `by-design` /
`deferred` / `documented` / `partial` limitations and all live behaviors STAY in the live file.

---

<!-- B-005-1 — closed: SUPERSEDED 2026-05-29 by 013 (ResendRequest recovery) -->
- **B-005-1 — A too-high `MsgSeqNum` gap was session-fatal as shipped in 005 — SUPERSEDED 2026-05-29 by 013, which added ResendRequest recovery.** As 005 shipped, a detected inbound gap surfaced `session_seqnum_gap_unrecoverable`, emitted a `Logout`-with-text, and disconnected (no ResendRequest). **013-session-reconnect-binding replaced this with AwaitingResend + real ResendRequest recovery in Active (B-013-1)**; only the pre-Active handshake gap remains handshake-fatal. *(FR-008, amended 013 T048 / PR #86; Session-2026-05-18.)*

<!-- L-005-2 — closed: resolved by 033 (FIXT.1.1 establishment) -->
- **L-005-2 — FIXT.1.1 / 5.0SP2 establishment conformance was not claimed by 005; only FIX.4.2/4.4 were validated at 005.** **Status: resolved (033)** — FIXT.1.1 establishment shipped in 033. *(FR-017; SC-001.)*

<!-- L-005-3 — closed: resolved by 021/022 (PossDup tolerance) -->
- **L-005-3 — A too-low seqnum without `PossDupFlag=Y` is always session-fatal (`session_seqnum_too_low`, `session.cpp:1694`); PossDup duplicate semantics were out of 005 scope.** **Status: resolved (021/022)** — PossDup tolerance shipped in 021/022; the residual (a too-low *without* PossDup stays session-fatal by design) is intended, not deferred. *(US2#2; spec §Edge Cases.)*

<!-- L-005-4 — closed: resolved by 015 (multi-session runtime engine) -->
- **L-005-4 — One `Session` models exactly one counterparty pair (a permanent 005 design fact); 005 itself shipped no multi-session registry or acceptor demux.** **Status: resolved (015)** — the multi-session runtime engine / registry shipped in 015. *(Key Entities "Session".)*

<!-- L-014-2 — closed: resolved by 015 (live acceptor identity + seam removal) -->
- **L-014-2 — [RESOLVED in 015] At 014 the CompID-binding decision was proven on the live path only for the initiator; the acceptor relied on the `logon_peer_identity_override` test seam, so T-041 did not fully close in 014.** 014 wired the live identity into `authorize()` and proved it only on the live initiator reconnect path. The live acceptor path and test-seam removal — and full T-041 production closure — landed with **015**: `logon_peer_identity_override` is removed from production (grep-empty at HEAD; the acceptor now drives identity through `live_peer_id_`). **Status: resolved (015).** *(FR-008; spec §Assumptions "T-041 advances but does not fully close"; `engine_seam_removal_test.cpp`.)*

<!-- L-014-3 — closed: resolved by 015 (multi-session runtime engine) -->
- **L-014-3 — The public multi-session Initiator/Acceptor runtime engine is out of scope; 014 is per-session wiring through the existing `ReconnectFsm`.** No public accept/connect-loop component, no `SessionConfig`-keyed registry, no programmatic multi-session lifecycle, no acceptor accept→Session-create→byte-feed production path — all carved out to 015 at the time of 014. The continuous inbound read-pump rebind was also 015. **Status: resolved (015)** — the multi-session runtime engine shipped in 015. *(spec §Out of Scope; contract C1.)*

<!-- L-015-2 — closed: RESOLVED 2026-06-11 by 016 (busy-spin + stop-cancel) -->
- **L-015-2 — An initiator pointed at a DOWN peer IS promptly torn down (RESOLVED
  2026-06-11; both halves fixed in 016, PR #93 `52d4180`).** Historically the
  per-session reconnect FSM used a default `ReconnectPolicy{}` with an **empty schedule**
  (0 backoff) ⇒ an unbounded busy-spin at ~100% CPU on repeated connect failure (**cause #1**);
  and an in-flight `async_connect` was not cancelled by total-cancel, so a
  *mid-connect, never-established* initiator blocked `Engine::stop()` until the 30 s
  connect timeout ran to completion (**cause #2**). **Both are fixed.** Cause #1:
  `resolve_reconnect_policy()` defaults to `ReconnectPolicy::defaults_quickfix_compat`
  (non-zero backoff — `src/session/session.cpp:103-117`). Cause #2: an OUT
  cancellation-state filter maps any accepted cancellation to `terminal` on the in-flight
  connect (`src/transport/asio_tls_transport.cpp:912-927`), so `stop()`'s
  `cancellation_type::total` promptly aborts it. **Proof:** the `DownPeerWatchdog` cell
  (`tests/interop/happy/hp_down_peer_stop_watchdog_test.cpp`, ctest #316) aims an
  initiator at a SYN-black-holed peer (RFC 5737 `192.0.2.1:9`) and asserts `stop()`
  returns in <1.5 s against the 30 s `connect_timeout` — measured ~32 ms.
  **Established** sessions stop cleanly (B-015-2). *(012-transport
  `async_connect`-cancellation; [[feedback_engine_stop_must_close_transports_total_cancel_insufficient]].)*

<!-- L-016-1 — closed: resolved by 019+020 (live business-message interop) -->
- **L-016-1 — RESOLVED (2026-06-11). The session-only `016` badge did NOT discharge the
  `[const §VII.6]` business-message interop clause; `020` + live validation now do.**
  `Logon → NewOrderSingle → ExecutionReport → Logout` was **not** exercised by `016`
  (v1.0 interop was session-layer only — the open v1.0-GA residual carried in `plan.md`
  R7). It is now **DISCHARGED**: the 019 `Application`-callback layer + 020 typed
  builders are driven live `Logon→NOS→ExecRpt→Logout` vs **both** QuickFIX-J and
  QuickFIX-cpp, both roles, by the 4 gated `BM-*-fix44-nos-execrpt` cells (`status: pass`,
  `matrix_disposition: live`) with banked goldens (`phase-9-harness/golden/BM-*.fix`),
  satisfying 020 SC-003. **Status: resolved** — see catalogue `[const §VII.6]` note +
  `A-001/A-006` (which stay `backlog` only for full-field / all-version codegen coverage).
  *(FR-005/FR-027/SC-008.)*

<!-- L-019-3 — closed: LIFTED 2026-06-06 by 023 (per-session/control strand) -->
- **L-019-3 — ~~Callbacks are serialized by single-thread engine-executor confinement; a
  multi-threaded `io_context` is NOT supported this slice.~~ LIFTED by 023.** Engine-driven
  session entry points now run on a per-session strand (the whole role loop —
  establish/handshake/read-pump/callbacks/sends/both teardown closes — is `co_spawn`'d on
  `SessionEntry::session_strand`, and the transport I/O object is bound to that strand), and
  all engine-global control-plane state is serialized on a distinct engine **control strand**
  (registry/listeners/endpoints/counters/handle-publication + `stop()`'s teardown reads).
  **A multi-threaded `io_context` is now safe under the default configuration (SC-005).**
  **Status: LIFTED 2026-06-06 by `023-engine-session-strand`.** Earned by the FULL witness
  set passing under a clean ASan ∧ UBSan ∧ TSan matrix (exact set, not a subset —
  `[[feedback_completeness_gate_exact_set_not_subset]]`): **V-1 ∧ V-2 ∧ V-3 ∧ V-8 ∧ V-9 ∧
  V-10 ∧ V-11 ∧ V-12** (per-session teardown serialization, MT business-message round-trip
  acceptance, cross-session parallelism, control-plane public-reader race fixed by the
  D-SNAP snapshot, re-entrant-send no-deadlock + post-stop fast-fail, transport-on-strand at
  all four ctor sites, MT-safe snapshot readers + bounded-handle lease, stop-before-publish).
  TSan full suite 388/388; engine_session_strand + business_messages_roundtrip green ×3
  sanitizers. Fixes the flaky `BIO_ctrl` SEGV/UAF teardown crash
  (`[[project_business_roundtrip_bio_ctrl_segv]]`). *(FR-010/SC-005; research.md D0–D8;
  data-model.md E-0…E-7; contract C-0…C-8 / V-1…V-12.)*

<!-- L-021-2 — closed: SUPERSEDED by 022 (allow_pos_dup strip shipped) -->
- **L-021-2 — The send-path `AllowPossDup` strip knob (FR-008) is DEFERRED.** This slice is
  INBOUND-ONLY. Stripping caller-supplied `43`/`122` on a plain `send` is NOT a toggle of an
  existing seam: the opaque `send_impl` copies the business body verbatim, so it requires a NEW
  boundary-anchored `43`/`122` excision parser with a delimiter-injection hostile witness
  (same hazard class as 020 RC#1) before it can ship. Intended default = STRIP, auto-resend
  always re-adds. The `allow_poss_dup` `SessionConfig` field is NOT added in this slice (only
  `redeliver_poss_dup` is). **Status: deferred** (FR-008 / research.md D7 — own future
  opaque-send-hardening slice). **SUPERSEDED by 022 (B-022-1): the knob + excision shipped.**

<!-- L-024-1 — closed: discharged by 025 (RefreshOnLogon S-018 shipped) -->
- **L-024-1 — `RefreshOnLogon` (S-018) is NOT implemented.** *(DISCHARGED by 025 — see S-018
  catalogue row and L-025-1 below for the remaining per-reconnect re-hydrate caveat.
  Historical record preserved below.)*
  fixpp's `SeqnumManager` was never store-seeded at `open()` (it started at 1; the only
  `set_next_inbound` caller was the inbound SequenceReset handler), so there was no
  construction-time store cache to refresh on reconnect. A meaningful `RefreshOnLogon` needed
  a store→manager hydrate-on-open path (an `008`-boundary change) fixpp did not yet have.
  **The `008`-boundary dependency was discharged by 029 (S-042)**; the `RefreshOnLogon` knob
  (`refresh_on_logon=true`) was shipped by 025 (S-018). **Status: discharged** (025, S-018
  → `done`). *(Clarifications Q3; contract C7.1; catalogue S-018.)*

<!-- L-033-5 — closed: RESOLVED 2026-06-17 by 042 (open-time serviceability guard) -->
**L-033-5 — RESOLVED by 042 (2026-06-17).** A FIXT session (acceptor or initiator) whose configured
`default_appl_ver_id` cannot be served by the engine `version_registry` now fails closed at
`Session::open()`-time with `error::invalid_session_config` — before any observable state mutation or
wire emission. The operator footgun (L-033-5's "silently rejects every inbound FIXT Logon") is
eliminated: misconfiguration is reported immediately at config-load, like QuickFIX's
`DataDictionary`-presence check.

**Implementation**: a third disjunct on the FQ-1 FIXT guard at `src/session/session.cpp` (guard block;
`!app_version_registry_->get(*cfg_.default_appl_ver_id).has_value()`). Reuses the same serviceability
predicate as the inbound runtime check (see below). Role-agnostic — applies to both acceptor and initiator
(042 FR-008 / `specs/042-fixt-version-serviceability-guard/spec.md`).

**Inbound peer-`1137` check unchanged**: the existing runtime check on the **peer-advertised**
`DefaultApplVerID(1137)` (033 FR-004a — `Reject(35=3, 371=1137, 373=5)`) remains live and unmodified.
A correctly-configured FIXT session whose peer advertises a version this side's registry cannot serve
still refuses at runtime with the same `373=5` reject — witnessed by
`FixtLogonEstablishment.W4_042_InboundNonDeadness_PeerUnserviceableSurvives`.

**Witnesses**: `W1_042_AcceptorOpenFail_UnserviceableDefault` (acceptor RED-first, mutation-tested),
`W2_042_InitiatorOpenFail_UnserviceableDefault` (initiator, role-agnostic, mutation-tested),
`W3_042_Serviceable_BothRolesOpenSucceed` (non-regression, both roles),
`W4_042_InboundNonDeadness_PeerUnserviceableSurvives` (inbound non-deadness / SC-003).
All in `tests/session/test_fixt_logon_establishment.cpp`.

<!-- L-050-1 — closed: discharged by Feature C / 052 (public dict loader) -->
**L-050-1 — productive pure-C dictionary loading is blocked on Feature C; Feature B's round-trip uses a test-supplied dictionary.** A session requires a non-null dictionary and `Session::open` has no engine-default fallback (`session.cpp:925-931`), but the only dictionary *producers* are C++ (`XmlLoader`) and the file-loading C-ABI (`fixpp_dict_load_*`) is Feature C. SC-001 is therefore demonstrated with a dictionary injected through a **test-only seam** (`tests/capi/capi_loopback_support.hpp::make_test_dict_handle` → the real `fixpp_session_config_set_dictionary` setter). No pure-C consumer can obtain a dictionary until Feature C ships. **Status: by-design boundary; discharged by Feature C.** *(050 spec Q/RC#2; SC-001.)*

<!-- L-050-z — closed: RESOLVED by 050 Gate B R4 (EngineState reclaim) -->
**L-050-z — `fixpp_engine_destroy` heavy-state reclaim.** The tombstone round-1 fix retained the *entire* `fixpp_engine` object (including `ioc_`, `work_guard_`, `clock_`, `engine_`, `workers_`) in `s_dead_shells` forever, which was an unbounded per-cycle leak under engine create/destroy churn. **Status: RESOLVED — Gate B R4 (`050-c-abi-session-send-recv`).** `fixpp_engine_destroy` now reclaims the heavy members by splitting `fixpp_engine` into a retained *identity shell* and a `std::unique_ptr<EngineState> state_` that is freed (`state_.reset()`) after workers are joined and the C++ `Engine` is stopped/destroyed. Only the small per-engine identity shell (`tag_` + `sessions_` + `app_`) remains in `s_dead_shells` for tombstone idempotency — `sessions_` and `app_` must survive for the process lifetime because `fixpp_session_t*` consumer pointers borrow into `sessions_` storage, and `session->slot` borrows into `app_->slots_` (freeing either would reintroduce the round-1 UAF). The retained shell is an O(handles) bounded cost, not the full `io_context` graph. A live-instance counter witness (`EngineStateReclaimedOnDestroy` in `tests/capi/lifecycle_test.cpp`) confirms `EngineState` is reclaimed on each destroy; `DestroyIsIdempotentSamePointer` and `PostEngineDestroySessionHandleIsInvalidHandle` (both ASan-verified) confirm the borrow invariants are intact. *(050 Gate B R4; `src/capi/capi_internal.hpp` `EngineState`; `src/capi/engine.cpp` `fixpp_engine_destroy`.)*

<!-- L-054-2 — closed: CLOSED by python-bindings hardening (UBSan lane) -->
**L-054-2 CLOSED — the `python-bindings` Tier-1 matrix now has a UBSan lane (`ubsan`), retiring the 053 D-9 / W-1 carried waiver.** A fourth `python-bindings` leg builds the extension + trampoline + static deps with `-fsanitize=undefined` (`FIXPP_PYTHON_SANITIZER=ubsan`, mirroring the asan/tsan legs; the module links a SHARED ubsan runtime via rpath so import needs no LD_PRELOAD) and runs the full suite under `UBSAN_OPTIONS=halt_on_error=1`. The whole suite was proven UBSan-clean locally under clang-22 (82 tests, zero findings) — the anticipated CPython C-API aliasing noise did not materialise, so no suppressions file is needed; a finding on this lane is a real defect. **Status: closed by the python-bindings hardening PR.** *(053 D-9; `bindings/python/CMakeLists.txt`; `.github/workflows/tier1.yml` python-bindings matrix.)*

<!-- 053-note — closed: CLOSED (msg_get_string non-UTF-8 decode witnessed) -->
**053 `msg_get_string` argout codec waiver CLOSED — the decode-side non-UTF-8 path is now witnessed.** The 053 Gate-B waiver ("`msg_get_string` argout codec witness unwitnessable in-scope") is retired: `test_msg_get_string_non_utf8_routes_through_fixpp_error` hand-builds an app payload carrying a genuinely non-UTF-8 field value (the binding wraps no raw-bytes setter), round-trips it through the two-engine loopback, and asserts the inbound `msg_get_string` decode raises `fixpp.Error` "not valid UTF-8" (the `PyUnicode_FromStringAndSize == NULL` branch in `fixpp.i`), not a bare `UnicodeDecodeError`. Mutation-tested: dropping the `_str == NULL` guard makes the read surface a `SystemError`, failing the witness. Companion to the encode-side `test_codec_failure_routes_through_fixpp_error`. **Status: closed by the python-bindings hardening PR.** *(053 D-9 / FR-008 / T-3; `bindings/python/tests/test_roundtrip.py`.)*

<!-- L-053-1 — closed: CLOSED by PY-004 (flyweight invalidation) -->
**L-053-1 CLOSED — inbound Python message flyweights are invalidated at callback return, so a stashed post-dispatch read raises `fixpp.ObjectLifetime` (1202) instead of reaching a use-after-free.** The OO trampoline now builds an inbound `fixpp.Message` flyweight with a strong `Session` parent ref, then arms `msg._dead = True` before releasing the GIL on every callback exit path, including the exception-exit path where the callback raised. `Session.close()` and `Engine.close()` also invalidate tracked child messages before native teardown. **Status: closed by shipped PY-004 behavior.** *(055 FR-003/004/017, SC-001; `bindings/python/fixpp_oo.py`; `bindings/python/tests/test_lifetime.py`.)*

<!-- L-062-1 — closed: RESOLVED by 063 (Defect A: per-context membership) -->
**L-062-1 — RESOLVED (063, commit `0caafd23` + witness `tests/codegen/nested_group_read_test.cpp::NestedGroupRead.RealDictionaryMassQuoteTwoQuoteEntriesPerInstancePrices`):** ~~nested typed reads of a group whose NumInGroup tag is REUSED with differing membership in the FIX dictionary return the wrong membership against `Dictionary::as_table_view()` until 063 (pre-existing Defect A).~~ The XML loader registers repeating-group membership globally, first-XML-occurrence-wins, keyed only by the NumInGroup tag (`src/dictionary/xml_loader.cpp:476-516`, `if (!group_index_by_no_tag_.contains(no_tag)) …`). FIX legitimately reuses a NumInGroup tag for different group definitions in different components — confirmed in `dictionaries/FIX44.xml`: tag **295** heads both `QuotCxlEntriesGrp` (earlier) and `QuotEntryGrp` (later, used by **MassQuote**), so `group_member_tags(295)` first-seen-resolves to the wrong (QuotCxl) variant and `MassQuote.quote_sets()[s].quote_entries()` returns size 0 against the real dictionary. Not reachable via the hand-built `table_view` the 062 witnesses use (they specify membership explicitly); reachable by any caller parsing MassQuote via `Parser{Dictionary::as_table_view()}`. **063 Defect-A fix (shipped):** per-context group registration (`(msg_type, parent_path, no_tag)`, `group_context` — `src/dictionary/dictionary.cpp::as_table_view()`, `include/fixpp/dict/table_view.hpp`, `include/fixpp/wire/group_view.hpp`), with a census of every reused NumInGroup tag across FIX44/FIX42/FIX50SP2 (`tests/dictionary/reused_tag_census_test.cpp`). Proven end-to-end (real `FIX44.xml` via `Dictionary::as_table_view()`, generated `fixpp::v44::MassQuote` accessors, SC-001b/C-3). *(062 US2 / T015 `NestedQuoteEntriesPerInstancePrices` GTEST_SKIP (now un-skipped, 063 T026); findings 2026-07-05 §Defect A.)*

<!-- L-062-2 — closed: RESOLVED by 063 (Defect B: nesting-aware extent) -->
**L-062-2 — RESOLVED (063, commit `88ad2763` + witness `tests/codegen/nested_group_read_test.cpp::NestedGroupRead.NestedQuoteEntriesPerInstancePrices` / `RealDictionaryMassQuoteTwoQuoteEntriesPerInstancePrices`):** ~~nested typed reads of an outer group instance that contains a nested group with >1 entries return TOO FEW entries until 063 (pre-existing Defect B — `OffsetTable::group()` not nesting-aware).~~ `OffsetTable::group()`'s instance-boundary walk used a flat `seen_in_instance` duplicate-tag heuristic with NO nested recursion (`src/wire/offset_table.cpp:419-437`, pre-063): when an outer instance contains a nested repeating group whose delimiter/fields legitimately repeat (>1 nested entries), the walk truncated the outer slice at the 2nd nested entry. Confirmed: a MassQuote QuoteSet[0] with 2 QuoteEntries yielded a truncated outer slice stopping before the 2nd entry's `299`, so `quote_entries().size()==1` not 2. 062's nested MECHANISM faithfully re-slices whatever outer slice it is handed, so it was fed a truncated slice — single-entry-per-occurrence nesting was unaffected (no descendant-tag repeat within one occurrence), which is why the 062 depth-3-collision + dict-aware-trailing witnesses passed while the multi-entry `NestedQuoteEntriesPerInstancePrices` was honestly `GTEST_SKIP()`'d. Pre-existing on `main` (062 only ADDED `build_nested_subview`/`nested_group_slices`, never touched `group()`). **063 Defect-B fix (shipped):** the boundary walk (`consume_group_extent`, `src/wire/offset_table.cpp`) recursively consumes a nested group's extent (via its own count/delimiter), depth-bounded (`kMaxGroupDepth=16`), alloc-free, before resuming the outer detection. Proven both in isolation (hand-built dict, 063 T026) and end-to-end against the real dictionary (063 T027, SC-001b/C-3). *(062 US2 / T015; findings 2026-07-05 §Defect B; needed Defect A's correct membership to know which tags are nested counts.)*

---

## Second wave (2026-07-08) — verified stale by the Codex/Fable re-assessment pass

<!-- B-007-2 — closed: FALSE / closed by 041 — Engine::start() now returns expected_t<void> and calls validate_engine_config() unconditionally, rejecting a null clock with clock_not_set (engine.cpp:1139, engine_config.hpp:188-190). The unwired behavior no longer exists. Verified by Codex 2026-07-08. -->
- **B-007-2 — There is NO active engine-level null-`clock` rejection: the `clock_not_set` gate (FR-006) is specified via `validate_engine_config()` but UNWIRED in the shipped runtime `Engine`.** `validate_engine_config()` (`engine_config.hpp:188`) has zero production callers (test-only); the runtime `Engine` does not gate on a null `EngineConfig::clock` at construction or start. A null clock surfaces only as a downstream defensive disconnect / "should not happen" guard (`session.cpp:4289`, `:4710`), not a clean `clock_not_set` at open — the same unwired-gate class as `wire::Validator` (B-004-1). *(FR-006; US2.1; `engine_config.hpp:188`; zero-caller grep.)*

<!-- L-024-2 — closed: RESOLVED (032) + live-closed 2026-06-12 — the initiator outbound restore-to-2 shipped; T021/SC-003 live close-out is DONE (RL-{QFcpp,QFj}-init cells now status: pass, matrix_disposition: live; cell_results.yaml:132-139). Verified by Codex 2026-07-08. -->
- **L-024-2 — A `reset_on_logon=true` INITIATOR rebases its OUTBOUND seqnum 2→1 on the
  peer's `141=Y` echo (live-found, RESOLVED — 032).** A fixpp
  initiator with `reset_on_logon=true` resets to `{1,1}`, emits `Logon(141=Y, 34=1)`
  (outbound advances 1→2), then receives the peer's Logon ack which — per QuickFIX-cpp
  and QuickFIX-J — echoes `141=Y`. fixpp's initiator Logon-ack handler treats that echo
  as a peer-requested reset (`session.cpp:3185`, `peer_ack_sent_reset_flag` arm) and calls
  `reset_seqnums_to_one_durable()`, which rebases BOTH counters to 1 — so **outbound
  regresses 2→1** and the next outbound frame would carry a duplicate `MsgSeqNum=1` (the
  031 duplicate-seqnum hazard; both real engines would reject it). 030 restored the
  INBOUND twin on this arm (`session.cpp:3360`) but left the outbound twin unfixed; the
  echo of fixpp's *own* reset should not trigger a second reset at all. **Invisible to
  in-process units** — the merged unit `ResetOnLogon_Initiator_ResetsAndEmits141`
  (`test_reset_on_lifecycle.cpp:390`) asserts the correct `peek_outbound()==2` but never
  processes a peer `141=Y` echo; the divergence surfaces only on the first live run
  (`RL-{QFcpp,QFj}-init-fix44-reset-on-logon`, both engines fail identically). The
  ACCEPTOR cells (`RL-*-acc`) are unaffected (030-fixed) and live-green. **Status: RESOLVED —
  unit+wire proven; live close-out pending (T021).** The initiator `peer_ack_sent_reset_flag` arm now
  restores OUTBOUND to 2 after the echo-confirmed reset (Mechanism A: restore-after-reset — the
  outbound twin of 030's inbound restore, `set_next_outbound(seqnum_min+1)` +
  `persist_outbound_advance_`, fatal-when-persistent) iff fixpp itself emitted the reset Logon at
  seq 1 (latched emit-time fact `own_logon_sent_reset_flag_` AND `reset_before_send`). The
  skip-the-reset alternative this entry originally speculated was **rejected at Gate A** (unsound
  for a fresh `bilateral_strict`-at-`{1,1}` initiator whose only durable reset on the path is this
  ack-arm reset). The harm test is now live:
  `tests/session/test_persistent_seqnum_hydrate.cpp` →
  `ResetOnLogon_Initiator_PeerAck141_OutboundStaysTwo` (asserts `peek_outbound()==2` + the SC-002
  wire witness `34=2`, no duplicate `34=1`); the 2 init interop cells are **expected to flip** from
  `deferred:initiator-141echo-outbound-rebase` to pass once the live cell is run (T021/SC-003 live
  close-out PENDING — separate live-interop session, same pattern as 030/031). See B-032-1.
  *(`src/session/session.cpp:3185`; sibling of 030/031; found 2026-06-11, fixed 032.)*

<!-- L-050-4 — closed: DISCHARGED by 051 — the [1400,1499] session/app C-ABI error block shipped (error.h:165-179); translate() re-points ordinals 119/77/129/130/131 off UNKNOWN (error.cpp:131-134/218-223); B-051-3 explicitly discharges it. Verified by Codex 2026-07-08. -->
**L-050-4 — the published `[2i §4.3]` session/app C-ABI error block is DEFERRED; the reachable `session_*`/`app_*` send/open arms map to `FIXPP_ERR_UNKNOWN`.** `[2i §4.3]` publishes no session/app code block, so re-pointing the 5 reachable arms (`session_invalid_argument` 119, `session_invalid_state_for_send` 77, `app_do_not_send` 129, `app_callback_threw` 130, `app_payload_malformed` 131) off `UNKNOWN` would require editing the signed-off `[2i]` (out of scope, contradicts the Gate-A LEAVE/CHK030). User decision 2026-06-24: DESCOPE. No new `error.h` codes / no `translate()` re-point / no `error_codes_v1.txt` append / no occupancy delta. The existing-published send arms (`wire_frame_too_large`→`WIRE_LIMIT_EXCEEDED`, `store_seqnum_overflow`→`STORE_RUNTIME`, `session_already_closed`→`THREAD_SESSION_LIFECYCLE`, cancellation→`CANCELLED`) are unchanged. **L-049-2 stays open.** FR-015/SC-005 deferred with this block. **Status: documented v1.0 behaviour; awaits a dedicated `[2i §4.3]` amendment.** *(050 spec FR-015/SC-005; data-model.)*
