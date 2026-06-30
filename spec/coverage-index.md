# Spec Coverage Index

> Phase 1.6 output — 2026-05-07.
> Maps every normative FIX spec section → catalogue row IDs (bidirectional traceability).
> Format: one sub-table per document. Zero-coverage sections are flagged as gaps.
> Canonical Spec ref format: `[DocAbbrev §X.Y.Z] Section title`

---

## DocAbbrev Registry

| Abbrev | Document | URL |
|---|---|---|
| `FIX-SL` | FIX Session Layer (classic, online ed.) | fixtrading.org/standards/fix-session-layer-online/ |
| `FIXT` | FIXT.1.1 Transport Independence (sections §5.3 of FIX-SL) | embedded in FIX-SL §5 |
| `FIXS` | FIXS RC1 — FIX over TLS (online ed., v1.1 RC1) | fixtrading.org/standards/fixs-online/ |
| `FIX40`…`FIX50SP2` | FIX application spec per version (classic tag=value) | fixtrading.org/standards/ |
| `FIX-TC` | FIX Session Layer Test Cases (online ed.) | fixtrading.org/standards/fix-session-testcases-online/ |
| `FIX-Latest` | FIX Latest (living online standard) | fixtrading.org/standards/fix-latest-online/ |

**Note on FIXT:** FIXT.1.1 is not a separate online document; its normative content is embedded in `FIX-SL §4` (session rules apply to all profiles) and described in `FIX-SL §5.3` (FIXT session profile). References use `[FIX-SL §5.3.x]` for FIXT-specific items and `[FIX-SL §4.x]` for shared session rules.

---

## FIX Session Layer (FIX-SL)

Section structure sourced from fixtrading.org/standards/fix-session-layer-online/ (confirmed 2026-05-07).

| Section | Title | Normative? | Catalogue IDs | Gap note |
|---|---|---|---|---|
| §1 | Scope | N (informative framing) | — | covered by impl/constitution |
| §2 | Normative references | N (bibliography) | — | covered by impl/constitution |
| §3 | Terms and definitions | N (definitions) | — | covered by impl/constitution |
| §3.1 | General terms and definitions (23 terms incl. NextNumIn/Out, TestRequestThreshold, etc.) | N | S-008, S-009 | covered by impl/constitution; threshold values are rules-of-engagement |
| §4 | FIX session (root) | Y | S-001–S-036 (aggregate) | — |
| §4.1 | Sequence numbers | Y | S-009, S-042 | S-042 (029): durable inbound counter + bidirectional hydrate-on-open — persistent store seeded into `SeqnumManager` at cold open; inbound counter persisted after each delivery. |
| §4.2 | Identifying the FIX session | Y | S-016, S-020 | — |
| §4.2.1 | The FIX session profile | Y | S-020 | — |
| §4.2.2 | Identification of FIX session peers (CompID) | Y | S-016, S-040 | S-040 (028): `check_comp_id=false` skips the steady-state `49`/`56` match; Logon-establishment + BeginString + 013 authz still enforced. |
| §4.2.3 | Validation of SendingTime(52) | Y | S-019, S-039 | **S-019 extended by 038 (2026-06-15):** the MaxLatency check now covers the acceptor first-Logon path in addition to the established-session and initiator Logon-ack paths; absent/empty/malformed/stale `52` → `Reject(35=3, 371=52, 373=10)` + disconnect (pre-establishment shape, no Logout). Conforming path byte-identical. Witnesses: `tests/session/test_acceptor_logon_sending_time.cpp`. See B-038-1/L-038-1/L-038-2/L-038-3 in `behaviors-and-limitations.md`. |
| §4.2.4 | Additional fields available for peer identification (SubID, LocationID) | Y | S-016 | — |
| §4.3 | Establishing a FIX connection | Y | S-001, S-015, S-021, S-022 | **S-022 `backlog → done` via 033**: `Username(553)`/`Password(554)` emit (when configured) + inbound parse + surface via `authorize_logon` seam; `554` redacted. Unit witnesses green (`test_fixt_credentials.cpp` W6/W7); live interop (SC-003/SC-004) deferred to `/speckit-verify` self-run. |
| §4.3.1 | Transport layer requirements (TCP/IP, FIXS mandatory) | Y | T-001, T-002, T-042 | **T-042 (2026-06-17):** `asio_plain_transport` adds plain TCP transport (`insecure_plain_tcp` profile). The mandatory-TLS requirement of FIXS applies to production links; the plaintext path is gated behind a loud `[[deprecated]]` opt-in for colo/VPN-secured environments. See L-043-1, L-043-2. |
| §4.3.2 | Using the TestMessageIndicator(464) | Y | S-029 | — |
| §4.3.3 | Application layer encryption (deprecated EncryptMethod) | Y | S-021, T-042 | **S-021 amended by 043 T030 (2026-06-17):** `interpret_logon` now rejects inbound `98≠"0"`; present-but-malformed also fails closed. Unconditional / all profiles. Witness `tests/session/test_interpret_logon_encrypt_method.cpp` (4 cells). See B-043-1. |
| §4.3.4 | Heartbeat interval | Y | S-015 | — |
| §4.3.5 | Heartbeat interval determination | Y | S-015 | — |
| §4.3.5.1 | Acceptor requires specific heartbeat interval | Y | S-015 | — |
| §4.3.5.2 | Acceptor requires initiator specify range value | Y | S-015 | — |
| §4.3.5.3 | Acceptor accepts initiator specified interval | Y | S-015 | — |
| §4.3.6 | Maximum message size (MaxMessageSize 383) | Y | S-030 | — |
| §4.3.7 | Specifying application version (DefaultApplVerID 1137 / FIXT) | Y | S-025, S-026 | **S-025 `backlog → done` via 033**: FIXT.1.1 session establishment — `DefaultApplVerID(1137)` emit/parse/enforce (FR-001–FR-006); missing-1137 → `Reject(371=1137, 373=1)` (FR-003); unserviceable-1137 acceptor-side → `Reject` + Disconnect (NOT a Logout message) (FR-004a); `negotiated_version_profile()` accessor (FR-005). Unit witnesses green (`test_fixt_logon_establishment.cpp` W1/W2/W3/W5). **S-026 remains `backlog`**: inbound `ApplVerID(1128)` is TOLERATED per FR-010 (witness W8); per-message routing is a follow-on feature (L-033-1). Live interop (SC-004/SC-006) deferred to `/speckit-verify` self-run (US3 T025-T028). |
| §4.3.8 | Specifying supported message types (NoMsgTypes in Logon) | Y | S-037 | MISSING → row added (see Gap Summary) |
| §4.3.9 | Identification of application system and FIX session processor | Y | — | MISSING → row added (S-038) |
| §4.3.10 | Responding to FIX session establishment request (acceptor Logon ack / Logout reject) | Y | S-001 | — |
| §4.3.11 | Initial synchronization of messages (Logon seqnum check, ResendRequest on gap) | Y | S-014 | — |
| §4.3.12 | Synchronization after successful logon | Y | S-014, S-018, S-031, S-042 | S-042 (029): hydrate-on-open seeds both seqnum counters from the persistent store at cold start so the post-logon seqnum state is synchronized with the stored baseline. S-018 (RefreshOnLogon — per-reconnect re-hydrate knob) `backlog → done` via 025: `refresh_on_logon=true` re-reads BOTH counters from the store at each logon (force=true bypasses the 029 one-shot latch), store-wins unconditionally; suppressed under `bilateral_strict` (INV-RoL-3); default-off byte-identical no-op. FR-002 (per-2nd+-logon re-hydrate) is witnessed for the **initiator** role (W1/W2/W7); acceptor per-connection re-hydrate is handled by the 029 cold-hydrate spine (fresh Session per accept); the acceptor same-connection force-bypass is dead-but-harmless (see L-025-2). |
| §4.4 | Extended features for FIX session and connection initiation | Y | S-017, S-018, S-031, S-032, S-040, S-041 | S-017 (ResetOnLogon/Logout/Disconnect) done via 024; S-018 (RefreshOnLogon) `backlog → done` via 025 (per-reconnect re-hydrate knob, store-wins, suppressed under bilateral_strict; FR-002 witnessed for initiator; acceptor same-connection warm path deferred — L-025-2); S-031 **FIX 4.4 parity** shipped via 027 (`implementation-parity-4.4`); S-040 (`check_comp_id` knob) + S-041 (`validate_sequence_numbers` knob) shipped via 028; **FIXT.1.1 / 5.0SP2 version-gating delivered via 033** (S-025 done; S-022 done; S-020 FIXT half done; S-026 tolerate-only stays backlog). MaxLatency knob remains deferred. |
| §4.4.1 | Using NextExpectedMsgSeqNum(789) | Y | S-031 | **FIX 4.4 parity shipped (027)**: per-session knob; advertise 789 in Logon (both roles); honor peer 789 with proactive resend (X<N → resend [X,N-1], no ResendRequest round-trip); X>N or invalid → Logout+disconnect; default off byte-identical; behind-side tolerance (no at-logon ResendRequest suppression). Tests: `tests/session/test_next_expected_msgseqnum.cpp` + `tests/interop/happy/hp_fix44_next_expected_test.cpp`. **FIXT.1.1 / 5.0SP2 outstanding to G4** (row is versioned "5.0–5.0SP2, FIXT.1.1"; this slice is FIX 4.4 only). **030 amends**: the received-`141` acceptor reply 789 corrects 1→2 (derives from the 030-restored next-expected-inbound). **031 amends**: the acceptor honors the peer's 789 against its **pre-reply** next-outbound `N_pre` (parameterized `honor_peer_next_expected_(…, next_outbound_ref)`): in-sync `X==N_pre` ⇒ no resend (was a spurious GapFill at the reply-Logon seq → live reject); too-high boundary at `N_pre` (`X==N_pre+1` ⇒ Logout); genuine-gap range `[X, N_pre]` + initiator byte-identical. Witnesses W1 `Acceptor_XeqNpre_NoResend_Establishes` / W3 `Acceptor_XeqNprePlus1_TooHigh_Logout`; live close-out via the `NE-*-acc` cell. See B-031-1. |
| §4.4.2 | Using ResetSeqNumFlag(141) for 24-hour connectivity | Y | S-032 | **030 received-reset inbound advance correction**: a received `Logon(141=Y)` now advances next-expected-inbound to 2 (consumed seq-1 reset Logon is a surviving advance — QuickFIX reset-then-increment parity), on BOTH the acceptor and the initiator `peer_ack_sent_reset_flag` arms; OUTBOUND reply MsgSeqNum stays 1, reply 789→2 (027-on); persistent received-141 reset failure now fatal (amends 024 I-07). Tests: `tests/session/test_reset_on_lifecycle.cpp` (acceptor discriminating triple + initiator witnesses + persistent/non-persistent fault-injection split), value-pins in `test_reset_seqnum_policy_matrix.cpp` / `test_next_expected_msgseqnum.cpp` / `test_persistent_seqnum_hydrate.cpp`. **032 initiator OUTBOUND restore + latch-based `by_peer_request`**: the initiator `peer_ack_sent_reset_flag` arm now also restores OUTBOUND to 2 (guarded branch `own_logon_sent_reset_flag_ && reset_before_send` → `set_next_outbound(2)` + `persist_outbound_advance_`, fatal-when-persistent) and labels `by_peer_request` off the latch alone (the two gates are DISTINCT, diverging on `bilateral_strict`-at-N). Tests: `test_persistent_seqnum_hydrate.cpp` (W1 outbound-stays-2 + SC-002 wire `34=2`; W5 persist-fatal; W6 INV-H1 outbound; W8 hydrated latch counterexample), `test_reset_seqnum_policy_matrix.cpp` (W2 label-false, W3 peer-spontaneous-at-N, W4b `bilateral_strict`-at-N label-side, W7 fresh-no-knob-at-seq-1), `test_refresh_on_logon.cpp` (cross-reconnect latch lifecycle), + the 4-way latch-gate mutation matrix (T010a). See B-030-1/B-030-2/B-032-1. |
| §4.4.3 | Using ResetSeqNumFlag(141) during connection establishment | Y | S-032 | — |
| §4.4.4 | Using initiator state to restore acceptor session state | Y | S-014 | — |
| §4.5 | Message exchange during a FIX connection | Y | S-003, S-004, S-007 | — |
| §4.5.1 | FIX connection keep-alive (heartbeat) | Y | S-003, S-004 | — |
| §4.5.2 | Garbled message processing | Y | TC-003, S-009 | — |
| §4.5.3 | Missing sequence number (gap detection → ResendRequest) | Y | S-005, S-014 | — |
| §4.5.4 | Rejecting invalid messages (Reject 35=3) | Y | S-007, S-033, S-034 | 021 Arm-C/D session-`Reject(35=3)` carry `371=122`/`373=1` (RequiredTagMissing) and `371=122`/`373=10` (SendingTimeAccuracyProblem) — closes S-033's inbound enforcement; S-034 exercised (carries 371), disposition unchanged. **Amended by 036 (2026-06-14)**: the entire engine-originated `Reject(35=3)` family now fires the `Application::toAdmin` observation callback before transmit (previously 8 Reject sites bypassed it — FR-008 coverage gap); the engine-originated `BusinessMessageReject(35=j)` (A-014) routes through `toApp` (app message, veto-aware). See `feature-catalogue.md` APP-001 amendment + `behaviors-and-limitations.md` B-036-1. **Amended by 041 (2026-06-16)**: opt-in dictionary-driven inbound validation (`SessionConfig::validate_inbound_messages`, default OFF) now emits `Reject(35=3, 373∈{14 out-of-order, 2 unexpected-tag, 1 required-missing/group-structure, 5 type-nonconformant})` for dictionary violations on the live inbound path (wiring the previously-dead `wire::Validator` — B-004-1/B-005-7); validate-first per FSM arm, before the seqnum gate; reason 6 is a forward-looking guard unreachable with `pod_decimal` (L-041-3); enum-value checks deferred (L-041-1). `[2b §6.5]`, `[FIX50SP2 §2.1]`. See `feature-catalogue.md` W-014 amendment + B-041-1. |
| §4.5.5 | Test Request processing | Y | S-004, S-003 | — |
| §4.6 | FIX connection termination | Y | S-002 | — |
| §4.6.1 | Normal logout processing | Y | S-002 | — |
| §4.6.2 | Logout without acknowledgement (timeout → force disconnect) | Y | S-002 | — |
| §4.6.3 | Logout with retransmission of missed messages | Y | S-002, S-014 | — |
| §4.6.4 | When to terminate without Logout(35=5) (invalid BeginString/CompID cases) | Y | S-016, S-020 | — |
| §4.7 | Extended features for FIX connection termination | Y | S-031 | S-031 FIX 4.4 parity shipped via 027; FIXT/5.0 outstanding to G4. |
| §4.7.1 | Using NextExpectedMsgSeqNum(789) on invalid MsgSeqNum(34) | Y | S-031 | **FIX 4.4 parity shipped (027)**: X>N (peer's 789 exceeds our next-outbound) → Logout+disconnect; present-but-invalid 789 (parse→0, empty, non-digit) → Logout+disconnect (evaluated before X<N compare to close the [1,N-1] amplification path). **031 amends**: on the acceptor arm the X>N too-high comparison is now evaluated against the **pre-reply** next-outbound `N_pre` (so `X==N_pre+1` in the peer's initial Logon is correctly too-high, not mis-classified in-sync). **FIXT.1.1 / 5.0SP2 outstanding to G4.** |
| §4.8 | Message recovery | Y | S-011, S-012, S-013, S-014, S-041 | S-041 (028): `validate_sequence_numbers=false` suppresses gap detection and recovery — no `ResendRequest`, no too-low disconnect; frames delivered without seqnum advance. **S-013 amended by 035 (2026-06-14)**: `FileStore` disk I/O now genuinely offloaded to `file_io_executor` via `co_spawn` (was inert `post`, 012 D-18 — `[const §XV.4]` violation corrected); `generation_` guard added for mid-walk `reset()` races in `retrieve()`. See `feature-catalogue.md` S-013 amendment + `behaviors-and-limitations.md` B-035-1. |
| §4.8.1 | Ordered message processing | Y | S-009, S-014 | — |
| §4.8.2 | Request retransmission of messages (ResendRequest) | Y | S-005, S-024, S-041 | S-041 (028): `validate_sequence_numbers=false` suppresses `ResendRequest` on a forward gap — out-of-order frame is delivered-without-advance instead of entering AwaitingResend. **S-005/S-006 amended by 037 (2026-06-14):** resend-reply GapFill (`build_sequence_reset_gapfill`) now stamps `43=Y`+`122=52`; replay emitter (`build_replay_frame`) deduplicates stored `43`/`122` under `allow_pos_dup=true`. See B-037-1/B-037-2. |
| §4.8.3 | Responding to ResendRequest(35=2) | Y | S-005, S-014 | — |
| §4.8.4 | Possible duplicates (PossDupFlag semantics) | Y | S-010, S-033 | S-033 → **done** (021, inbound OrigSendingTime-required enforcement, Arms C/D). S-010 → **done** (021 inbound PossDup(43) too-low tolerance Arm A/B + 022 PossResend(97) inbound witness-confirmed + AllowPosDup send-knob FR-008 `allow_pos_dup` default-strip). **S-010 amended by 037 (2026-06-14):** `build_replay_frame` under `allow_pos_dup=true` now deduplicates stored `43`/`122` before re-appending (FR-004/FR-005). Default-path byte-identical. Live QFJ arm deferred (L-037-2); QFcpp waived (L-021-3). See B-037-2. |
| §4.8.5 | Gap fill process (SequenceReset-GapFill) | Y | S-006, S-041 | S-041 (028): `validate_sequence_numbers=false` bypasses the gapfill-mode `SequenceReset(35=4, 123=Y)` intercept — `NewSeqNo` not applied; frame delivered to `fromAdmin`, counter unchanged. **S-006 amended by 037 (2026-06-14):** `build_sequence_reset_gapfill` now stamps `PossDupFlag(43)=Y` + `OrigSendingTime(122)=SendingTime(52)` on every outbound GapFill. See B-037-1, L-037-1. |
| §4.8.5.1 | Example using SequenceReset(35=4) | Y | S-006 | — |
| §4.8.6 | Sequence reset (hard reset, GapFillFlag=N) | Y | S-006, S-023, S-041 | S-041 (028): `validate_sequence_numbers=false` bypasses the reset-mode `SequenceReset(35=4)` intercept — `apply_inbound_sequence_reset` not called; frame delivered to `fromAdmin`, counter unchanged. |
| §4.8.7 | Processing inbound possible duplicate messages | Y | S-010 | 021: inbound possible-duplicate processing delivered (tolerate too-low `43=Y` replay, no seqnum advance — Arm A; admin ignore / app drop(default)/redeliver(`redeliver_poss_dup`); too-low without `43=Y` stays fatal — Arm B). |
| §4.8.8 | Processing gaps for session layer messages (admin msg gap-fill) | Y | S-014 | — |
| §4.9 | Resending unacknowledged application message (PossResend 97) | Y | S-010 | 022: `PossResend(97)=Y` is an APPLICATION-level resend indicator — fixpp delivers it to `fromApp` (full MessageView, tag 97 readable), advances seqnum, never session-rejects for `97` (witness-only, matching QFcpp/QFJ which add no session-level handling). |
| §4.9.1 | Difference between application resend and session retransmission | Y | S-010 | 022: PossResend(97) (app-level, new in-sequence seqnum) is distinct from session retransmission (PossDupFlag(43), replayed seqnum + OrigSendingTime(122)); `97` is NOT subject to the 122-required rule (keys on `43=Y` only). |
| §4.10 | FIX session state matrix | Y | S-008 | — |
| §4.10.1 | FIX logon process state transition diagram | Y | S-008 | — |
| §4.10.2 | FIX logout process state transition diagram | Y | S-008 | — |
| §5 | FIX session profiles (informative) | N | — | informative; normative requirements embedded in §4 |
| §5.1 | FIX.4.2 session profile | N | S-020, D-001 | informative profile description |
| §5.2 | FIX4 session profile | N | S-020 | informative profile description |
| §5.3 | FIXT session profile | N | S-025, S-026 | informative; normative FIXT rules in §4.3.7. **033**: S-025 `done`; S-026 tolerate-only (S-026 stays `backlog`). |
| §5.3.1 | Profile identification (BeginString=FIXT.1.1) | N | S-020 | informative; **033** delivers the FIXT.1.1 half (see §4.2.1 gap note). |
| §5.3.2 | Multiple application version support | N | S-026 | informative |
| §5.3.3 | Session default application version identification (DefaultApplVerID 1137) | N | S-025 | informative; **033** delivers S-025 (see §4.3.7 gap note). |
| §5.3.4 | Message type default application version | N | S-026 | informative |
| §5.3.5 | Explicit application version per message (ApplVerID 1128) | N | S-026 | informative |
| §5.3.6 | Use of extension packs | N | — | out-of-scope → dropped(post-1.0: EP-level field additions) |
| §5.3.7 | Use of custom application version | N | — | out-of-scope → dropped(post-1.0: custom app versions) |
| §5.4 | LFIXT session profile | N | S-027, S-028 | informative profile description |
| §5.4.1 | LFIXT profile identification | N | S-027, S-028 | informative |
| §5.4.2 | LFIXT application version identification | N | S-027, S-028 | informative |
| §5.4.3 | LFIXT transport layer requirements | N | S-027 | informative |
| §5.4.4 | LFIXT compatible mode | N | S-027 | informative |
| §5.4.5 | LFIXT succinct mode | N | S-028 | informative |
| §6 | FIX message routing | Y | S-016 | — |
| §6.1 | Message routing — point-to-point | Y | S-016 | — |
| §6.2 | Message routing — third-party routing (OnBehalfOf/DeliverTo) | Y | S-016, TC-016 | 005-defer: `OnBehalfOfCompID(115)`/`DeliverToCompID(128)` third-party addressing is the non-49/56 portion of S-016 — deferred-with-traceability to a later third-party-addressing feature; 005 owns only the 49/56 point-to-point portion (see 005 ledger below) |
| §7 | Transmitting alternatively encoded messages | Y | — | out-of-scope → dropped(post-1.0: alt encoding framing; SOFH W-016 tracks SOFH framing) |
| §7.1 | Use of Attachment group | Y | — | out-of-scope → dropped(post-1.0: attachment group) |

---

## FIXT.1.1 (FIXT)

FIXT.1.1 is not a standalone online document; its normative session rules are unified into FIX-SL §4. FIXT-specific application version negotiation sections are §4.3.7, §4.3.8, §5.3. Key FIXT rows are indexed under FIX-SL above.

| FIXT Concept | FIX-SL Section | Catalogue IDs | Gap note |
|---|---|---|---|
| BeginString=FIXT.1.1 — session profile identification | §5.3.1 | S-020 | **033**: `BeginString=FIXT.1.1` accepted/emitted both roles; FIX.4.x sessions byte-identical no-op (FR-009/SC-002). Unit witnesses green (`test_fixt_logon_establishment.cpp` W1/W4); live interop SC-004/SC-006 **proven live 2026-06-12** (8 cells, QFcpp+QFJ × init+acc × {5.0SP2, 4.4-over-FIXT}, goldens banked — 033 US3). |
| DefaultApplVerID(1137) on Logon — default app version | §4.3.7, §5.3.3 | S-025, S-043 | **033**: `backlog → done`. Emit/parse/enforce (FR-001–FR-006); missing-1137 → `Reject` (FR-003); unserviceable acceptor-side → `Reject` + Disconnect (NOT a Logout message) (FR-004a); `negotiated_version_profile()` (FR-005). Unit witnesses green (W1/W2/W3/W5); live deferred (SC-004/SC-006). **042 (S-043)**: `open()`-time serviceability guard — a FIXT session whose configured `default_appl_ver_id` is not in the engine `version_registry` fails closed at `open()` with `error::invalid_session_config` (role-agnostic; closes L-033-5). The 033 FR-004a inbound peer-`1137` reject stays live. New guard branch: DA covered by W1_042+W2_042 (acceptor+initiator open-fail); non-regression W3_042 (serviceable open-success, both roles); inbound non-deadness W4_042 (peer v50sp1 absent → 373=5 still fires). |
| Username(553)/Password(554) credentials on FIXT Logon | §4.3, [FIXS §2.6] | S-022 | **033**: `backlog → done`. Emit when configured + inbound parse + surface via `authorize_logon` seam (FR-007/FR-008); `554` redacted. Unit witnesses green (`test_fixt_credentials.cpp` W6/W7); live deferred (SC-003/SC-004). Acceptor-side validation deferred (FR-008a / L-033-2). |
| ApplVerID(1128) per message — per-message app version override | §4.3.7, §5.3.5 | S-026 | **033 note**: inbound `1128` TOLERATED per FR-010 (witness W8 in `test_fixt_logon_establishment.cpp`); per-message routing remains `backlog` (L-033-1). |
| NoMsgTypes on Logon — supported message types advertisement | §4.3.8 | S-037 | MISSING → row added (S-037) |
| Multiple application version support | §5.3.2 | S-026 | — |
| Extension pack precedence rules | §5.3.6 | — | out-of-scope → dropped(post-1.0: EP precedence) |
| Custom application version | §5.3.7 | — | out-of-scope → dropped(post-1.0) |

---

## FIXS RC1 (FIXS)

Section structure sourced from fixtrading.org/standards/fixs-online/ (v1.1 RC1, confirmed 2026-05-07).

| Section | Title | Normative? | Catalogue IDs | Gap note |
|---|---|---|---|---|
| §1 | Introduction | N | — | informative |
| §1.1 | Scope | N | T-002 | informative |
| §1.2 | An overview of TLS | N | — | informative background |
| §1.3 | Network topologies and perspectives | N | T-008, T-009, T-010 | informative; normative rules in §2 |
| §1.4 | When and where to use FIXS | N | T-002 | informative |
| §1.5 | References | N | — | bibliography |
| §2 | Authentication Methods | Y | T-008, T-009, T-010, T-011, T-012 | — |
| §2.1 | Recommended authentication and key exchange methods | Y | T-008, T-009 | — |
| §2.2 | Mutual and Simple TLS protocol options | Y | T-008, T-009, T-010 | — |
| §2.3 | Leaf Certificate Pinning | Y | T-008, T-011 | — |
| §2.4 | Certificate Validation with CA Pinning | Y | T-009 | — |
| §2.5 | Pre-shared keys (PSKs) | Y | T-012 | — |
| §2.6 | FIX authentication (FIXA) | Y | S-022 | FIXA details not yet public; S-022 covers Logon credentials |
| §3 | Protocol Parameters | Y | T-006, T-007, T-013 | — |
| §3.1 | Protocol version (TLS 1.2 / 1.3 only; prohibit TLS 1.0/1.1/SSL) | Y | T-006, T-007 | — |
| §3.2 | Protocol features (compression disabled, renegotiation disabled, session caching) | Y | T-006, T-007 | — |
| §3.3 | Cipher suites (AES-GCM, CHACHA20, ECDHE; prohibit RC4, DES, anon, MD5) | Y | T-013 | — |
| §3.4 | Certificate parameters (RSA 2048-bit min, ECDSA 256-bit, X.509, expiration) | Y | T-039 | covered by `[2g §4.5]` verify_peer (cross-cut with 2h per `[2g §7.1]` / `[2g §A.2]`) |
| §3.5 | PSK properties (32-char min, out-of-band exchange, multiple simultaneous PSKs) | Y | T-012 | — |
| §3.6 | Application specific TLS (ALPN / SNI hooks) | Y | — | out-of-scope → dropped(post-1.0: ALPN/SNI application TLS) |
| §4 | Policies and Management | Y | T-011, T-012 | — |
| §4.1 | Sharing secrets (approved channels: HTTPS, GnuPG, PKCS#12, postal, in-person) | Y | T-040 | covered by `[2g §4.1]` cert_source::load_credentials + `[2g §4.2]` file_cert_source |
| §4.2 | Storing secrets (private keys, PSKs, pinned certs) | Y | T-040 | covered by T-040 |
| §4.3 | Renewing secrets (rotation support; multiple simultaneous during rotation) | Y | T-011 | — |
| §4.4 | Authorization linked to authentication (auth'd TLS identity ↔ FIX CompID) | Y | T-041 | covered by `[2g §4.5]` peer_identity (cross-cut with session-module Phase-4 per `[2g §7.2]` / `[2g §A.2]`) |
| Appendix A | Cipher Suites (reference table) | N | T-013 | informative |
| Appendix B | Relevant RFCs | N | — | informative |
| Appendix C | Known Vulnerabilities | N | — | informative |
| Appendix D | TLS Implementations | N | — | informative |

---

## FIX Application Layer (FIX40–FIX50SP2)

Indexed at message granularity (one row per MsgType) plus general rules sections.
The application spec is version-specific (FIX 4.0 through FIX 5.0SP2); sections referenced by functional area as common across versions.

### General Encoding Rules (common to all app versions)

| Section | Title | Normative? | Catalogue IDs | Gap note |
|---|---|---|---|---|
| §3 (wire) | Tag=Value encoding — field format, SOH delimiter, ASCII | Y | W-001 | — |
| §3.1 | Standard header (BeginString/8, BodyLength/9, MsgType/35, mandatory ordering) | Y | W-002, W-004, W-005 | — |
| §3.1 | Standard trailer (CheckSum/10 mandatory last) | Y | W-003 | — |
| §3.2 | Repeating groups (NoXxx delimiter, ordered field list, nested groups) | Y | W-006, W-007, D-010 | — |
| §3.3 | Field data types (28 types: STRING, CHAR, INT, FLOAT, BOOLEAN, UTCTIMESTAMP, etc.) | Y | W-009 | — |
| §3.3 | Data (raw bytes) field pairs (Length+Data atomicity) | Y | W-008 | — |
| §3 | Message framing — pipelined messages on TCP stream | Y | W-010 | — |
| §3 | Message serializer — BodyLength + CheckSum computation | Y | W-013 | — |
| §3 | Message validator — required fields, type conformance, enum values, group structure | Y | W-014 | — |

<!-- W-001..W-014 supplemental note (T057, 2026-05-17): the wire-encoding rows above
     are delivered by 004-wire-codec (PR #68). Bidirectional traceability:
     [FIX50SP2 §3]   ↔ W-001 (Tag=Value), W-010 (framing), W-013 (serializer), W-014 (validator);
     [FIX50SP2 §3.1] ↔ W-002 (header), W-003 (trailer), W-004 (BodyLength), W-005 (CheckSum);
     [FIX50SP2 §3.2] ↔ W-006/W-007 (repeating + nested groups);
     [FIX50SP2 §3.3] ↔ W-008 (Length+Data), W-009 (field data types).
     W-011 (zero-copy parser) / W-012 (offset-table) are `[impl] implementation NFR` rows —
     no FIX-spec section maps to them by design; their verifying tests are recorded in
     feature-catalogue.md. W-009's wire FLOAT-accessor leg (the 001-core-decimal 2b deferral)
     is closed by 004 T027. Verified GREEN per .specify/decisions/004-wire-codec-verify.md
     (run-2 FINAL @ ce1d4d2); Gate A converged r1; Gate B PR #68 converged 2026-05-17 (gate-b-done, HEAD 8253ef7, P1=0/P2=0/P3=0). -->

### Session-Layer Messages (MsgType catalogue, all versions)

| MsgType | Message | Catalogue ID | Gap note |
|---|---|---|---|
| 0 | Heartbeat | S-003 | — |
| 1 | TestRequest | S-004 | — |
| 2 | ResendRequest | S-005 | — |
| 3 | Reject | S-007 | — |
| 4 | SequenceReset | S-006 | — |
| 5 | Logout | S-002 | — |
| A | Logon | S-001 | — |

### Application Messages — Pre-Trade (FIX 4.0+)

| MsgType | Message | Catalogue ID | Gap note |
|---|---|---|---|
| 6 | IndicationOfInterest | R-002 | — |
| 7 | Advertisement | R-003 | — |
| B | News | R-004 | — |
| C | Email | R-005 | — |
| R | QuoteRequest | M-010 | — |
| S | Quote | M-009 | — |
| Z | QuoteCancel | M-011 | — |
| a | QuoteStatusRequest | M-012 | — |
| b | MassQuoteAcknowledgement | M-008 | — |
| i | MassQuote | M-008 | — |
| AG | QuoteRequestReject | M-010 | — |
| AH | RFQRequest | A-021 | — |
| AI | QuoteStatusReport | A-021 | — |
| AJ | QuoteResponse | A-021 | — |

### Application Messages — Market Data (FIX 4.2+)

| MsgType | Message | Catalogue ID | Gap note |
|---|---|---|---|
| V | MarketDataRequest | M-001 | — |
| W | MarketDataSnapshotFullRefresh | M-002 | — |
| X | MarketDataIncrementalRefresh | M-003 | — |
| Y | MarketDataRequestReject | M-004 | — |
| e | SecurityStatusRequest | M-006 | — |
| f | SecurityStatus | M-006 | — |
| g | TradingSessionStatusRequest | M-007 | — |
| h | TradingSessionStatus | M-007 | — |

### Application Messages — Reference Data

| MsgType | Message | Catalogue ID | Gap note |
|---|---|---|---|
| c | SecurityDefinitionRequest | M-005 | — |
| d | SecurityDefinition | M-005 | — |
| v | SecurityTypeRequest | A-025 | — |
| w | SecurityTypes | A-025 | — |
| x | SecurityListRequest | A-025 | — |
| y | SecurityList | A-025 | — |
| z | DerivativeSecurityListRequest | A-026 | — |
| AA | DerivativeSecurityList | A-026 | — |
| BK | SecurityListUpdateReport | A-029 | — |
| BP | SecurityDefinitionUpdateReport | A-029 | — |
| BR | DerivativeSecurityListUpdateReport | A-026 | — |
| BI | TradingSessionListRequest | A-027 | — |
| BJ | TradingSessionList | A-027 | — |
| BS | TradingSessionListUpdateReport | A-027 | — |
| BT | MarketDefinitionRequest | A-028 | — |
| BU | MarketDefinition | A-028 | — |
| BV | MarketDefinitionUpdateReport | A-028 | — |

### Application Messages — Trade (FIX 4.0+)

| MsgType | Message | Catalogue ID | Gap note |
|---|---|---|---|
| D | NewOrderSingle | A-001 | Partial G2 interop evidence (020-g2-business-messages): minimal FIX-4.4 typed NOS builder + live both-role interop vs QuickFIX-J/cpp. Full-field + all-version (4.2/5.0SP2/FIXT.1.1) coverage deferred (FR-015a/FR-015b). Row stays backlog. |
| E | NewOrderList | A-002 | — |
| F | OrderCancelRequest | A-003 | — |
| G | OrderCancelReplaceRequest | A-004 | — |
| H | OrderStatusRequest | A-005 | — |
| 8 | ExecutionReport | A-006 | Partial G2 interop evidence (020-g2-business-messages): minimal FIX-4.4 typed ExecRpt builder (fully-filled) + live both-role interop vs QuickFIX-J/cpp. Full-field + all-version (4.2/5.0SP2/FIXT.1.1) coverage deferred (FR-015a/FR-015b). Row stays backlog. |
| 9 | OrderCancelReject | A-007 | — |
| K | ListCancelRequest | A-019 | — |
| L | ListExecute | A-019 | — |
| M | ListStatusRequest | A-019 | — |
| N | ListStatus | A-019 | — |
| Q | DontKnowTrade | A-015 | — |
| k | BidRequest | A-020 | — |
| l | BidResponse | A-020 | — |
| m | ListStrikePrice | A-020 | — |
| q | OrderMassCancelRequest | A-008 | — |
| r | OrderMassCancelReport | A-009 | — |
| s | NewOrderCross | A-016 | — |
| t | CrossOrderCancelReplaceRequest | A-012 | — |
| u | CrossOrderCancelRequest | A-013 | — |
| AB | NewOrderMultileg | A-017 | — |
| AC | MultilegOrderCancelReplace | A-011 | — |
| AF | OrderMassStatusRequest | A-010 | — |
| BN | ExecutionAcknowledgement | A-018, A-024 | note: A-018 and A-024 are duplicates (same MsgType BN); see gap note |
| BZ | OrderMassActionReport | A-023 | — |
| CA | OrderMassActionRequest | A-023 | — |

### Application Messages — Post-Trade (FIX 4.0+)

| MsgType | Message | Catalogue ID | Gap note |
|---|---|---|---|
| J | AllocationInstruction | P-001 | — |
| P | AllocationInstructionAck | P-002 | — |
| T | SettlementInstructions | P-006 | — |
| o | RegistrationInstructions | R-001 | — |
| p | RegistrationInstructionsResponse | R-001 | — |
| AD | TradeCaptureReportRequest | P-008 | — |
| AE | TradeCaptureReport | P-008 | — |
| AK | Confirmation | P-005 | — |
| AL | PositionMaintenanceRequest | C-002 | — |
| AM | PositionMaintenanceReport | C-002 | — |
| AN | RequestForPositions | C-002 | — |
| AO | RequestForPositionsAck | C-002 | — |
| AP | PositionReport | C-002 | — |
| AQ | TradeCaptureReportRequestAck | P-008 | — |
| AR | TradeCaptureReportAck | P-008 | — |
| AS | AllocationReport | P-003 | — |
| AT | AllocationReportAck | P-004 | — |
| AU | ConfirmationAck | P-005 | — |
| AV | SettlementInstructionRequest | P-007 | — |
| AW | AssignmentReport | A-022 | — |
| AX | CollateralRequest | C-001 | — |
| AY | CollateralAssignment | C-001 | — |
| AZ | CollateralResponse | C-001 | — |
| BA | CollateralReport | C-001 | — |
| BB | CollateralInquiry | C-001 | — |
| BG | CollateralInquiryAck | C-001 | — |
| BH | ConfirmationRequest | P-005 | — |
| BL | AdjustedPositionReport | C-002 | — |
| BM | AllocationInstructionAlert | A-031 | — |
| BO | ContraryIntentionReport | A-022 | — |
| BQ | SettlementObligationReport | A-030 | — |

### Application Messages — Infrastructure (FIX 4.2+)

| MsgType | Message | Catalogue ID | Gap note |
|---|---|---|---|
| j | BusinessMessageReject | A-014 | — |
| n | XMLnonFIX | A-034 | — |
| BC | NetworkCounterpartySystemStatusRequest | N-001 | — |
| BD | NetworkCounterpartySystemStatusResponse | N-001 | — |
| BE | UserRequest | N-002 | — |
| BF | UserResponse | N-002 | — |
| BW | ApplicationMessageRequest | N-003 | — |
| BX | ApplicationMessageRequestAck | N-003 | — |
| BY | ApplicationMessageReport | N-003 | — |
| CB | UserNotification | A-032 | — |
| CC | StreamAssignmentRequest | A-033 | — |
| CD | StreamAssignmentReport | A-033 | — |
| CE | StreamAssignmentReportACK | A-033 | — |
| CQ | AccountSummaryReport | C-003 | — |

### Duplicate MsgType A-018 / A-024

Both A-018 and A-024 in the catalogue reference MsgType BN (ExecutionAcknowledgement). A-024 is a duplicate entry from a later pass. A-018 is the canonical row; A-024 is a dropped duplicate. See Gap Summary.

---

## FIX Session Test Cases (FIX-TC)

20 test scenarios defined (15 mandatory, 5 optional). All confirmed mapped to TC-001–TC-017.

| Scenario | Title | Mandatory? | Catalogue IDs | Gap note |
|---|---|---|---|---|
| §4 | FIX session layer test cases (container section) | Y | TC-001–TC-017 | — |
| 1B | Connect and Send Logon (Initiator) | Y | TC-001 | — |
| 1S | Receive Logon (Acceptor) | Y | TC-001 | — |
| 2S | Receive any message other than Logon | Y | TC-002 | — |
| 2 | Receive Message Standard Header | Y | TC-002 | — |
| 3 | Receive Message Standard Trailer | Y | TC-003 | — |
| 4 | Send Heartbeat message | Y | TC-004 | — |
| 5 | Receive Heartbeat message | Y | TC-004 | — |
| 6 | Send Test Request | Y | TC-004 | — |
| 7 | Receive Reject message | Y | TC-005 | — |
| 8 | Receive Resend Request message | Y | TC-006 | 005-defer: recovery-dependent — deferred-with-traceability to the later session-recovery feature (see 005 ledger below) |
| 9 | Synchronize sequence numbers | Optional | TC-014 | 005-defer: recovery-dependent — deferred-with-traceability to the later session-recovery feature (see 005 ledger below) |
| 10 | Receive Sequence Reset (Gap Fill) | Y | TC-007 | 005-defer: recovery-dependent — deferred-with-traceability to the later session-recovery feature (see 005 ledger below) |
| 11 | Receive Sequence Reset (Reset) | Y | TC-008 | 005-defer: recovery-dependent — deferred-with-traceability to the later session-recovery feature (see 005 ledger below) |
| 12 | Initiate logout process | Y | TC-009 | — |
| 13 | Receive Logout message | Y | TC-009 | — |
| 14 | Receive application or session layer message | Y | TC-010 | 005-partial: scenario-14 corpus is exactly `14a`–`14j`; 005 ships `14a`–`14g` (session-layer reject taxonomy) in scope; `14h_RepeatedTag`/`14i_RepeatingGroupCountNotEqual`/`14j_OutOfOrderRepeatingGroupMembers` are repeating-group/repeated-tag dictionary-validation cases deferred-with-traceability (see 005 ledger below) |
| 15 | Send application or session layer messages (field ordering) | Optional | TC-015 | — |
| 16 | Queue outgoing messages | Y | TC-011 | — |
| 17 | Support encryption (legacy EncryptMethod) | Optional | TC-017 | — |
| 18 | Support third-party addressing | Optional | TC-016 | — |
| 19 | PossResend handling | Y | TC-012 | S-010 protocol semantics **done** (021 PossDup(43) + 022 PossResend(97) — the latter witness-only, NOT recovery-dependent). The formal `[FIX-TC]` scenario-19 conformance cell (TC-012) remains backlog (conformance-harness scope, see 005 ledger below). |
| 20 | Simultaneous Resend request | Y | TC-013 | — |

**Conclusion:** All 20 FIX-TC scenarios map to TC-001–TC-017. TC-001 covers 1B+1S; TC-002 covers 2S+2; TC-004 covers 4+5+6; TC-009 covers 12+13. No FIX-TC catalogue-mapping gaps. **Per-feature delivery scope is not full per PR:** feature `005-session-establishment-fsm` ships only the capability-partitioned in-scope subset green and records the rest deferred-with-traceability — see the **005 session-establishment — scope-deferral ledger** below (these are recorded, traceable scope deferrals, not silent omissions).

---

## 005 session-establishment — scope-deferral ledger

> Recorded for `[const §I.4]` (no silent omission) and the `[const §VII.5]` Gate-A blocker waiver (Art XVII §1) carried by feature `005-session-establishment-fsm`. Feature `005` ships only the in-scope `[FIX-TC]` subset green this PR; the entries below are deferred-with-traceability to the named successor work. `[const §VII.5]` (full TC corpus per PR) is NOT satisfied by 005 and proceeds under an explicit recorded Gate-A blocker waiver (`[const §XVII.1]`, `constitution.md:255`); see `specs/005-session-establishment-fsm/plan.md` Constitution Check + Complexity Tracking.

| Deferred item | Catalogue / scenario | Reason | Discharged by |
|---|---|---|---|
| Too-high `MsgSeqNum` oracle cases (`1a_ValidLogonMsgSeqNumTooHigh`, `2b_MsgSeqNumTooHigh`) | TC-001/TC-002 (QFJ `fix42`/`fix44`) | Require the deferred `ResendRequest(35=2)` to pass the QFJ comparison; 005 treats too-high as session-fatal (Logout-with-text → disconnect), recovery is out of scope | later session-recovery feature |
| ResendRequest / SequenceReset-GapFill / SequenceReset-Reset / synchronize-seqnums | scenarios 8, 10, 11, 9 → TC-006/TC-007/TC-008/TC-014 | Recovery-dependent (gap-fill / store-recovery) | later session-recovery feature |
| PossDup / PossResend duplicate semantics (S-010) | scenario 19 → TC-012 | Recovery-dependent duplicate handling | **S-010 semantics DONE** (021 PossDup(43) + 022 PossResend(97)/AllowPosDup); only the formal `[FIX-TC]` scenario-19 conformance cell (TC-012) remains → later conformance-harness work |
| Scenario-14 repeating-group/repeated-tag cases `14h`/`14i`/`14j` | scenario 14 → TC-010 | Repeating-group / repeated-tag dictionary-validation territory, not session-layer reject taxonomy; 005 ships `14a`–`14g` | later dictionary-validation / wire follow-up |
| S-016 third-party addressing `OnBehalfOfCompID(115)`/`DeliverToCompID(128)` | `[FIX-SL §6.2]`, scenario 18 → S-016/TC-016 | Separable session-routing work; 005 owns only the 49/56 point-to-point portion of S-016 | later third-party-addressing feature |
| Version-scope: FIX.4.0/4.1/4.3/5.0 establishment + FIXT.1.1/5.0SP2 (no oracle dir; `DefaultApplVerID(1137)` is `[FIX-SL §4.3.7]` / S-025) | S-001/S-008/S-009/S-015/S-016/S-019/S-020 + S-025 (1137) | QFJ oracle has no `fixt11`/`fix50sp2` dir; 4.0/4.1/4.3/5.0 are runtime-XML-only with no typed namespace in v1.0 (`[const §I.1]`); 005 validates FIX.4.2/4.4 only; the FIXT.1.1/5.0SP2 1137 half was delivered by **033** (S-025). [038: corrected stale `§4.4`/S-020-as-1137 → `§4.3.7`/S-025; S-020 = BeginString/version-profile `[FIX-SL §4.2.1]`.] | later version-coverage / FIXT work |

> **Per-row delivery scope (delivered slice).** Catalogue rows `S-001`, `S-002`, `S-003`, `S-004`, `S-007`, `S-008`, `S-009`, `S-015`, `S-016`, `S-019`, `S-020` — and the folded `core/` time-helper row #4 (`utc_time_to_fix_string` / `fix_string_to_utc_time` for `SendingTime(52)`) — are delivered by 005 as the **FIX.4.2/4.4 point-to-point establishment slice only**, not their full version span. `S-016` is delivered as the 49/56 portion only; the `OnBehalfOfCompID(115)`/`DeliverToCompID(128)` portion is the third-party-addressing deferral above. FIXT.1.1 / 5.0SP2 establishment is **explicitly NOT claimed** by 005 (FR-017 + SC-001 — the QFJ oracle has no `fixt11`/`fix50sp2` directory and FIXT logon-time semantics, including `DefaultApplVerID(1137)`, are deferred).
>
> **Ledger close-out (2026-05-22, Phase 8 T065).** The entries above are complete against the Session-2026-05-17 Q2 → Session-2026-05-18 re-scope and the Gate-A `[const §VII.5]` Article-XVII §1 recorded waiver carried by 005. No further deferrals are introduced by 005 Phases 1–8; any successor session-recovery / version-coverage / third-party-addressing feature discharges its rows by amending this ledger at merge.
>
> **009-session-fsm-finalize close-out (2026-05-23, T029).** Closed the FR-001 / FR-002 / FR-003 / FR-011 / FR-013 binding-contract drifts on 005's rows per `[[project_005_phase8_completeness_false_pass]]` (PR #81 round-1 hostile-review findings). 005 deferral ledger unchanged: no new deferral row, no green-via-009 of a deferred case. Slice scope was implementation-drift closure against the Gate-A-converged 005 design (Gate A not re-run; the design is unchanged). See `.specify/decisions/009-session-fsm-finalize-verify.md` for the YELLOW non-RED verify verdict; one pre-existing 005-baseline ASan UAF (`src/session/session.cpp:116`, `Session::open` config-ref lifetime) recorded as W-5 for a follow-on slice.
>
> **010-session-cfg-lifetime close-out (2026-05-23, T029).** Closed the W-5 + F-04 + F-05 + F-06 + F-07/E1 + F-11 + RC#G-mixed-path waivers on PR #82 Gate B (009 decision record). W-5 = pre-existing 005-baseline ASan stack-use-after-scope on `Session::cfg_` (declared `const SessionConfig& cfg_` at `include/fixpp/session/session.hpp:312`, dangling-dereferenced at `src/session/session.cpp:116`); resolved at `/speckit-clarify` 2026-05-23 with Option A (by-value `SessionConfig cfg_;`) — implementation discovery (mid-/implement T006) required FR-001a amendment: `SessionConfig::store_factory` flipped from `unique_ptr<MessageStoreFactory>` to `shared_ptr<MessageStoreFactory>` to make `SessionConfig` copy-constructible (the factory is a stateless interface; sharing across Sessions is meaningful; per-Session MessageStore uniqueness preserved). Gate A NOT re-run — inherited from 005 with a 010-specific addendum (`.specify/decisions/010-session-cfg-lifetime-gatea.md`, local-only). The `session_coverage_adversarial` ASan-only test-skip added in PR #82 as the W-5 carry-forward is now REMOVED. Adds one new public C++ symbol (`Session::fsm_visit_history()`) and one new error variant slot (`error::session_invalid_state_for_send = 77`). 005 design unchanged otherwise. See `.specify/decisions/010-session-cfg-lifetime-verify.md` for the verify verdict; the 009 W-1..W-4 coverage carry-forwards re-waived per `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]` + PR #73 precedent.
>
> **010 /simplify Wave 1-3 + F4 close-out (2026-05-23).** A second pass (`/simplify` with 3 review agents) caught the 13th instance of the completeness-PASS-as-hypothesis burn class (`[[project_005_phase8_completeness_false_pass]]` + `[[feedback_simplify_pass_catches_9th_burn]]`) recurring inside this very slice. All 11 findings remediated in-slice; see `specs/010-session-cfg-lifetime/simplify-fix-plan.md` for the full execution log. Notable in-slice closures: (a) **B-1** Session ctor `noexcept` removed — UB-class trap (copy ctor can throw via `std::string` / `std::function` / `std::shared_ptr` allocations → `std::terminate`); matches `close()` precedent. (b) **B-8 / FR-009** initiator handshake symmetry — `Session::open()`'s 3 emit-failure branches now `record_state_transition_(Disconnected)` matching the acceptor send-throw witness + the liveness-loop assign-failure precedent. (c) **F4 / W3.3-final** — the pre-existing 005 spec-vs-impl gap on `"A"` (dup-Logon-in-Active) + `"2"` (RR) + `"4"` (SeqReset) in `is_session_admin` is CLOSED in-slice (Codex 2nd-opinion + QuickFIX-cpp + QuickFIX/J survey converged on Position A — see `specs/010-session-cfg-lifetime/codex_f4_review.md`). All three MsgTypes now route through the Reject branch per 005 FR-017 "never silent no-op". **Forward upgrade obligation:** when the deferred session-recovery feature (catalogue row 400 above) lands, the RR/SeqReset cells UPGRADE Reject→Process (gap-fill via the message store), matching QuickFIX-cpp `Session::nextResendRequest` and QuickFIX/J `Session.nextResendRequest`; the dup-Logon cell stays as Reject per 005's intentional defensive divergence from QuickFIX refresh-on-dup-Logon convention.

---

## 017-log-otel — MERGED (PR #98 squash `09a9ae1`, 2026-06-03; /speckit-verify YELLOW, gate-b-waived)

> Phase-4 observability feature carved from anchor `.specify/2k-log-otel.md` v0.5. Two new modules — `fixpp::log` (zero-alloc MPSC `Logger`, 256-B `Record` / 24-B `ArgValue`, 3-tier trace macros, 4-method `Sink` + File/Otlp/Syslog) and `fixpp::otel` (Tracer/Meter SDK wrappers, `SessionSpans`, Prometheus + OTLP dual export). Owns catalogue rows **LOG-001..004 + OBS-001..003** (`spec/feature-catalogue.md` → Logging & Observability; all `done`). Layering `log→{core}`, `otel→{core,log}`, `otel↛transport` (check_layers GREEN).
>
> **Requirements coverage.** FR-001..023 (23) and SC-001..008 (8) each map to a landed test + landed impl (completeness audit 2026-06-03, 100%): producer zero-alloc/latency (FR-001/002 — `test_compile_cutoff_zero_alloc.cpp` TS-1, `bench/log/log_enqueue.cpp` TS-9), ring/overflow (FR-003/004 — `test_overflow_drop_newest.cpp` TS-2, `test_block_overflow_raw_thread.cpp` TS-3), drain thread + effective clock (FR-005/006), Sink + FileSink/SyslogSink (FR-007/008/009 — `test_file_sink_rotation.cpp` TS-4, `test_file_sink_async_fsync.cpp` TS-5, `test_syslog_sink.cpp`), filtering (FR-010/011 — `test_level_and_category_filter.cpp` TS-8), trace correlation + macros (FR-012/013 — `test_trace_correlation.cpp` TS-6/7, `test_log0_raw_thread.cpp`), shutdown (FR-014 — `test_shutdown_async_flush.cpp`), error block (FR-015 — `tests/core/test_017_error_completeness.cpp`, 7 enumerators slots 122–128), SessionSpans (FR-016 — `test_session_spans.cpp` TS-12), dual metric export (FR-017 — `test_dual_metric_export.cpp` TS-11), OtlpLogSink (FR-018 — `test_otlp_log_sink.cpp` TS-10), OTel wrappers + no-op fallback (FR-019 — `test_engine_close_teardown.cpp`), C-ABI placeholders (FR-020 — `include/fix/c_api/{log,otel}.h`, no symbols), TS-13 spike disposition (FR-021 — `bench/log/log_spike.cpp`, recorded in verify Part 2), non-goals (FR-022), build scaffold (FR-023 — `opentelemetry-cpp/1.26.0` pinned). SC-008: all 13 TS seams (TS-1..TS-13) exist AND execute.
>
> **2d-surface touch (minimal, no session-FSM wiring).** `Session::get_trace_context()` / engine trace-context accessors (`tests/session/test_trace_context_accessors.cpp`, `tests/core/test_trace_context_{engine_fallback,resume}.cpp`), `SessionConfig::{logger,tracer}_override` (`tests/session/test_017_session_config_amendment.cpp`), `EngineConfig` fwd-stub completion.
>
> **Verify YELLOW (pragmatic scope, user-approved 2026-06-03).** Full debug suite GREEN (22/22 incl. 18/18 log+otel); ASan/UBSan/TSan triad GREEN on the concurrency-critical OTel-free log subset (11/11 each, 0 findings); producer zero-alloc dual gate (counting_resource + mallocnesia) GREEN under all sanitizers. **DEFERRED to GA (waivers):** the OTel-instrumented sanitizer/release matrix (per-profile `opentelemetry-cpp` source build, only Debug cached) + the full coverage number + the bench soft-gate. Records: `.specify/decisions/017-log-otel-{verify,completeness,gatea}.md` (gitignored) + phase-4 lifecycle doc.

---

## 015-runtime-engine — Merged (PR #88 squash `d7e215b`, 2026-06-01; Gate B converged 4 fixer rounds [Sonnet 2/2 → Codex 2/2] + 1 confirmatory re-review, gate-b-waived)

> Public Initiator/Acceptor runtime engine in the existing `session/` module (no new module). Ships the acceptor accept→Session-create→byte-feed production path, `SessionConfig`-keyed session registry, programmatic multi-session lifecycle, and **closes catalogue row T-041** (`implementing → done`): both FIX roles bind the live `handshake_result.peer_id → authorize()` and fail CLOSED symmetrically; the test-only `logon_peer_identity_override` seam is removed from production AND tests (SC-006/FR-009; `engine_seam_removal` grep gate proves zero references). Dynamic-session-provider deferred (static matching closes T-041).
>
> **C++ surface published.** `fixpp::session::` additions: `Engine` (accept/connect loops, registry, `register_session`/`lookup`/`start`/`stop` — single-executor confinement, E-5; no `engine_strand_`), `SessionId`, `Session::attach_accepted_transport` (acceptor live-attach: sets `live_peer_id_` + rebinds outbound, no FSM transition), `Session::drive_reconnect()` + `Session::live_transport()` (initiator connect-then-Logon). New error slot `session_unknown_acceptor_session = 121` (FR-005/006: reject inbound Logon whose reversed CompIDs match no registered acceptor). Public delta bounded (SC-010 grep-confirmed). Live outbound goes through one serialized `write_gate_` (`fixpp::sync::async_mutex`) channel — ≤1 in-flight `async_write`, `shared_ptr<Transport>` keepalive, replay routed through it, write errors surfaced to the FSM.
>
> **Test surface.** `tests/session/engine_*` (acceptor, acceptor_failclosed, connect, firstframe, lifecycle, readpump, seam_removal, session_id, harness_compile_smoke) + `test_live_outbound_serialized.cpp` (serialization + liveness-drain UAF + terminal/graceful close-deadlock witnesses) + re-pointed `test_compid_binding_*` (off the removed seam onto live identity via `tests/support/identity_injecting_transport.hpp`). §4.4 coverage row maps T-041.
>
> **Verify YELLOW + Gate B.** `/speckit-verify` (on `c4bf7af`): full 6-preset matrix GREEN (ASan/UBSan/TSan 0 findings) — but it ran PRE-Gate-B, so the Gate-B live-write code's full-matrix ASan/TSan re-run is **owed at next `/speckit-verify`** (the 3 new lifetime witnesses passed ASan/TSan individually; CI later caught a *test-only* span-UAF in `live_outbound_serialized`, fixed `6eff4ea`). Completeness 100% (FR 14/14, SC 11/11). Gate B: R1-2 Sonnet (acceptor lazy-construct + `lookup()` contract; first live-send-serialization cut; lying-`engine_strand_` removal), R3-4 Codex (liveness counter armed before `co_spawn` [TOCTOU UAF]; 6 emit-failure sites→Disconnected; terminal then graceful `close()` socket-close before write-gate drain). Records: `.specify/decisions/015-runtime-engine-{verify,completeness,gateb}.md` (gitignored) + phase-4 lifecycle doc + `research/reviews/{codex,opus}_pr88*`.

---

## 016 interop-harness — production-touch ledger

> Tests-only feature with one bounded production prerequisite from T008. This
> ledger records traceability for that touch only; it does not claim the live
> QuickFIX matrix has run.

| Production touch | Spec section / requirement | Catalogue row(s) | Evidence / disposition |
|---|---|---|---|
| `SessionConfig::reconnect_policy` field (`include/fixpp/session/session_config.hpp`), `resolve_reconnect_policy()` in `src/session/session.cpp` around line 93, and bounded/cancellable connect/stop behavior in `src/session/engine.cpp` | `[FIX-SL §4.4]` reconnection / `ResetSeqNumFlag=N` continuity; 016 FR-004 and FR-028 | S-014 (session recovery / reconnect flow), S-032 (ResetSeqNumFlag policy, once row is populated) | `tests/session/reconnect_policy_witness_test.cpp` and `tests/interop/happy/hp_down_peer_stop_watchdog_test.cpp` prove finite reconnect policy and bounded `Engine::stop()` for the down-peer regression. Article IX §1 95/85 assessment is seeded for `/speckit-verify`; sanitizer full-matrix execution remains a 016 SC-004 parent/verify obligation. |

## 018-interop-live-admin — production-touch ledger

> Tests-only feature, **ZERO production touch** (gap-fill G1). No `src/`/`include`
> change — the engine send path, liveness loop, recovery sub-protocol, and Reject
> path already shipped (005/013/S-023). This ledger records that G1 adds live-QFJ
> interop **witnesses** (not new behaviour) for existing rows, and does not claim
> the live QuickFIX matrix has run (cells skip-with-reason absent the parent harness).

| Production touch | Spec section / requirement | Catalogue row(s) | Evidence / disposition |
|---|---|---|---|
| **NONE** (R-prod escape hatch did not fire) | `[FIX-SL §4.5.1/§4.5.5/§4.5.4/§4.5.3/§4.8.2/§4.8.5/§4.8.6]` admin/recovery; 018 FR-001..FR-011 | S-003/S-004/S-005/S-006/S-007/S-014/S-023 (live-QFJ interop witnesses) | `tests/interop/happy/hp_fix44_{testrequest_echo,idle_heartbeat_cadence,seqnum_recovery,recovery_outbound_answer,reject_invalid_admin}_test.cpp` — 10 G1 cells (5 groups × both roles) skip-with-reason without QFJ; each carries an SC-004 gate-bite (self-contained, passes). Goldens captured at first paired run (parent). Sanitizer full-matrix (ASan/UBSan/TSan) on the interop ctest is the `/speckit-verify` step (T024) — discharges the orthogonal 016 verify-YELLOW waiver. |

---

## 013-session-reconnect-binding — Merged (PR #86 squash `bd84e08`, 2026-05-29; Gate B converged 3 rounds, Sonnet 2/2, gate-b-waived)

> Session-Phase-4 surface: reconnect FSM driver + recovery sub-protocol + CompID↔TLS-identity binding + TLS-validation-outcome SessionEvent + in-process credential rotation. Ships catalogue rows **S-005 / S-006 / S-014 / S-024 → `done`** (recovery sub-protocol: ResendRequest issue+reply, SequenceReset-GapFill, ResetSeqNumFlag(141) 3-mode matrix, recovery FSM). Completes co-owned **T-039 / T-040 → `done`** (012 shipped the wiring half; 013 binds TLS outcome → `SessionEvent` and adds in-process `reload_credentials`). **T-041** stays `implementing` — policy/extraction/fail-CLOSED-when-mTLS Logon gate SHIPPED; production `handshake_result.peer_id → Session` source wiring needs the live `TlsTransport` → 014.
>
> **C++ surface published.** `fixpp::session::` additions: `ReconnectFsm` (recovery FSM driver on 005's 6-state `fsm_state` — AwaitingResend transient sub-state per D-1), `ResendState`, `CompIdAuthorizationPolicy` (allow-list, default-deny) + `extract_principal` (canonical CN→SAN-DNS→SAN-URI→SHA-256), `SessionEvent` variant union (5 alternatives: peer_identity_bound / compid_authorization_failed / tls_validation_failed / credentials_rotated / sequence_numbers_reset) + `Session::recent_events()` 16-entry ring, `SeqnumManager::reset_to_one()` (production 141=Y reset), `Session::reload_credentials` forwarder; new pure-virtual `TransportFactory::reload_credentials` (count 1→2, under 5/5 cap). 4 new `error::session_*` slots 116..119 + dual-emission of 005-era slots 73/74.
>
> **Error envelope.** `session_seqnum_reset_mismatch = 116` (FR-017 bilateral_strict), `session_compid_unauthorized = 117` (FR-021), `session_testreqid_mismatch = 118` (FR-006), `session_invalid_argument = 119` (FR-033). Slots 73/74 reused for FR-008/FR-004 FSM-driven timeouts (reference-engine sweep: zero precedent for typed discrimination). **Known carry-forward**: slot-74 `session_test_request_unanswered` is a documented stand-in for the seqnum-too-high path → dedicated `session_seqnum_too_high` deferred to v1.0 taxonomy gate.
>
> **Test surface.** 19 test files in `tests/session/` + `tests/transport/` + `tests/perf/` + `tests/fuzz/`. Highlights: `fsm_matrix_witness_test.cpp` (49 cells / 133 asserts), `test_reset_seqnum_policy_matrix.cpp` (15 cells incl. counter-reset + reply-frame-141Y assertions, false-pass closures), `test_recovery_store_horizon.cpp` (>1024B replay-intact), `test_compid_binding_mtls_fail_closed.cpp` (RC#A fail-CLOSED-when-mTLS), `test_tls_validation_failed_taxonomy.cpp`. All drive bytes through the FSM via `mock_transport::Script` (production-shaped, FR-038). CI matrix GREEN across debug/release/asan/ubsan/tsan/coverage/gcc/python (codecov/patch soft-gate waived per `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]`).
>
> **Gate B (3 rounds, Sonnet 2/2).** RC#A fail-CLOSED mTLS CompID footgun; RC#B >1024B resend-replay truncation; RC#C ResetSeqNumFlag(141) full FR-017/FR-018 conformance (incl. round-2 "lying event" — reset event emitted while counters never reset → production `reset_to_one()`); RC#D logout-timeout config wiring. Post-merge fix `8e2d362`: `[const §XV.9]` corpus regression (session.hpp awaitable closure dragged tls/pinset.hpp std::shared_mutex via reconnect_fsm.hpp → fixed by fwd-decl). 4 waivers (ReconnectFsm stubs → 014; fabricated-payload P3; slot-74 stand-in + doc drift; T-041 full wiring → 014). Record: `.specify/decisions/013-session-reconnect-binding-gateb.md` + phase-4 lifecycle doc.

---

## 012-2h-transport — Merged (PR #85 squash `53e25b1`, 2026-05-27; Gate B converged 4 rounds with 3 carry-forward waivers)

> Established the new `include/fixpp/transport/` + `src/transport/` module. Ships catalogue rows **T-001 / T-002 / T-003 / T-004 / T-005 / T-009 / T-010 → `done`** (post-MVP US2/US3/US4 + Gate B RC#A/RC#B closure landed everything originally deferred). Co-owns **T-006 / T-007 / T-008 / T-011 / T-013** (TLS protocol + cipher + pinset; 011 shipped surface, 012 ships wire) — flipped from `implementing` to `done`. **T-039 / T-040** stay `implementing` as "2h-owned wiring half SHIPPED" — full row pending session-Phase-4 binding into `SessionEvent`. **T-041** stays `backlog` pending session-Phase-4.
>
> **Final scope (cumulative across MVP + post-MVP slices + Gate B).** US1 (P1 TLS-encrypted FIX session end-to-end) + US2 (ReconnectPolicy schedule-array shape Q2=C + `delay_for_attempt(n)` + `defaults_quickfix_compat()`) + US3 (Listener + asio_listener with listener-owned cached SSL_CTX factory per RC#B Codex r3) + US4 (mock_transport test seam) + Phase 7 011 F-1 carryover discharged (8-cell × 4-sanitizer = 32 PASS witness) + Appendix D §D.1..§D.8 cross-doc amendments (T046-T048 in commit `81ab659`) + operator-quickstart `docs/src/transport-quickstart.md` (T051) + RC#A live TLS handshake Cells 7-8 + RC#D live truncated-close witness + RC#E FR-007 exclusivity Cells 1-4 + RC#F seam-13 D-17 reset + shared `tests/transport/loopback_tls_fixture.hpp`.
>
> **C++ surface published (MVP slice).** Six public types under `fixpp::transport::`:
> - `Transport` (abstract; 5 pure-virtuals — `async_connect` / `async_read_some` / `async_write` / `cancel` / `close`) + nested `Transport::Config` + `ConnectInfo` POD per `[2h §4.1]`.
> - `TlsTransport` (virtually inheriting `Transport`; 1 additional pure-virtual `async_handshake`) + `handshake_result` owning POD (peer_id by value, captured_pinset shared_ptr, negotiated_cipher pmr::string) per `[2h §4.2]`.
> - `Endpoint` value type — `{host, port, backlog=128}`, IPv6 zone-id admitted per FR-018 + `[2h §4.3]`.
> - `TransportFactory` (abstract; 1 pure-virtual `make(exec, ssl_cfg, mr) noexcept -> expected_t<unique_ptr<Transport>>`) per `[2h §4.7]`.
> - `asio_tls_transport` v1.0 reference impl (1099 LoC `src/transport/asio_tls_transport.cpp`) — OpenSSL `SSL_CTX` configured at construction (`SSL_CTX_set_min/max_proto_version` TLS 1.2/1.3 + ciphersuites + curves + sigalgs + `SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION | SSL_OP_NO_TICKET`; SSL_OP_NO_EARLY_DATA waiver pending — see below); `verify_peer_trampoline` extracts captured pinset via `SSL_set_ex_data` per Appendix D §D.7 and dispatches to `fixpp::tls::verify_peer`; ASIO awaitable surface with `cancellation_type::total` total-reset per D-17; in-flight exclusivity per FR-007; FR-026 factory caching of `SslCtxConfig` + `SSL_CTX*` + PMR root + engine clock; FR-028 destroy-before-mint reconnect contract.
> - `asio_tls_transport_factory` default impl (`src/transport/transport_factory.cpp`) + free `make_asio_tls_transport(exec, cfg, ssl_cfg, mr) noexcept -> expected_t<unique_ptr<Transport>>` wrapper for non-construction-time callers (2i C-ABI / runtime reconnect mint).
>
> **Error envelope.** 22 new `error::transport_*` variants at slots **94..115** per `[2h §6.6]:1167-1204`, partitioned into 5 coalescing groups for C-ABI: **LIFECYCLE** (8 — resolve_failed, connect_refused, connect_timeout, already_connected, already_closed, read_in_progress, write_in_progress, reconnect_limit_exceeded), **IO** (5 — read_eof, read_truncated, read_error, write_short, write_error), **HANDSHAKE** (2 — handshake_failed grouping variant carrying OpenSSL error + tls_* sub-reason per FR-034a, handshake_timeout), **CONFIG** (2 — factory_failed, psk_unsupported), **CANCELLED-reuse** (5 — connect/read/write/handshake/accept_cancelled → reuse `FIXPP_ERR_CANCELLED`). C-ABI coalescing emission is owned by 2i.
>
> **Test surface (MVP slice).** 14 new test binaries in `tests/transport/` + `tests/perf/` + `tests/conformance/` + 3 bench binaries (scaffold bodies — Tier-1 fill-in deferred) + 2 fuzz harnesses (libFuzzer 30s smoke under ASan, no crashes). All 8 wired Phase 3a binaries + 1 Phase 7 binary GREEN under Clang Debug + ASan + UBSan + TSan + GCC Release sanity. Highlights:
> - `tests/transport/test_load_credentials_seam13_witness.cpp` — **8-cell × 4-sanitizer = 32 PASS** witness discharging 011 Gate B F-1 carryover per FR-033 + SC-008 + Clarifications Q3=C. Cases 1-4 (cached-fast-path / cancel-before-pickup / cancel-during-handler / happy) × executor modes (per_session_strand / direct_executor). Cell-by-cell record at `.specify/decisions/012-2h-transport-verify.md §T044`.
> - `tests/transport/test_tls_handshake_pinset_rotation.cpp` — 4 cells (snapshot-stable-after-rotation, two-snapshots-independent, empty-snapshot, handshake_result field check).
> - `tests/transport/test_inflight_exclusivity.cpp` — 3 cells (FR-007 in-flight exclusivity error codes + namespace alias consistency).
> - `tests/perf/test_transport_read_alloc_guard.cpp` — DUAL gate (`counting_resource` PMR routing + mallocnesia weak symbols) per `[[feedback_tracking_pmr_resource_false_pass]]`.
> - `tests/perf/test_socket_option_defaults.cpp` — FR-029 / FR-029a initiator-leg socket option defaults (TCP_NODELAY=true, SO_LINGER disabled, tcp_keepalive=false); acceptor-leg cell deferred to US3.
> - `tests/transport/test_cancellation_propagation.cpp` — 3 runnable error-code cells + 8 DISABLED_ integration cells (4 async methods × 2 executor modes) pending live-wire fixture.
>
> **Waivers (final — applied at Gate B convergence per `[const §IX.1]` rationale-backed):**
> - **W-1 (12) SSL_OP_NO_EARLY_DATA omission** — constant absent in pinned OpenSSL 3.6.2 headers; 0-RTT off by ASIO default. FR-016 + `[FIXS §3.2]` + `[const §XII.3]` anchor.
> - **RC#C depth (Gate B carry-forward)** — `test_verify_peer_pmr_oom.cpp` witness exhausts boundary only; mid/tail SAN-DNS/SAN-URI/peer_identity + `verify_peer_trampoline` 7-PMR-container surface unwitnessed. Test cert has 1 SAN-DNS + 0 SAN-URI. Per `[[feedback_trap_throw_pmr_witness_enumerate_sites]]` + 011 PR #84 W-5/W-6 precedent.
>   - **CLOSED 2026-05-30 by 014-transport-active-binding (PR #87, FR-014):** `test_verify_peer_pmr_oom.cpp` adds the `leaf_multi_san.pem` fixture (3 SAN-DNS) + a `VerifyPeerPmrOomMultiSan` suite witnessing boundary/mid/tail PMR-OOM sites (N=1/2/3); re-witnessed **GREEN 9/9 under ASan** on `chore/repo-wide-lint` HEAD (2026-06-01). See `.specify/decisions/014-transport-active-binding-verify.md`.
> - **RC#G bench (Gate B carry-forward)** — `bench/transport/bench_tls_handshake_loopback.cpp` is scaffold; SetUp/TearDown TODOs; counters 0. Bench-body fill-in deferred until post-cache shape stable (which round-3 RC#B accept-path close just landed). Per 011 PR #84 W-2 cppcheck-bench precedent.
>   - **CLOSED 2026-05-30 by 014 (PR #87, FR-013b):** `bench_tls_handshake_loopback.cpp` is wired to the real `asio_tls_transport_factory` + loopback acceptor (`leaf_rsa2048.pem`/`ca.pem`, mtls_ca), with live `SetUp`/`TearDown` + a `Handshake1Rtt` loop emitting an `avg_us` counter — establishes the first baseline (soft/N/A gate per `014-transport-active-binding-verify.md`, **not** a ±5% regression gate this PR). Build + fixture SetUp re-confirmed 2026-06-01.
> - **RC#I fuzz doc (Gate B carry-forward)** — `fuzz_transport_read_path.cpp` documents reduced scope honestly (Framer::feed boundary, not asio_tls_transport::async_read_some); catalogue line label re-classification follow-on slice.
>   - **CLOSED 2026-05-30 by 014 (PR #87, FR-015):** fuzz scope re-labelled to actual post-MVP scope; both `tests/fuzz/fuzz_transport_handshake.cpp` and `fuzz_transport_read_path.cpp` present. Doc/catalogue-only — no production code change.
> - **W-4 (12) `asio_free` namespace alias** — `asio::async_connect` (free function) collides with `asio_tls_transport::async_connect` (member); resolved via local `namespace asio_free = asio` alias inside `asio_tls_transport.cpp`. Transparent — no behavior change; cosmetic only.
> - **Inherited from 011 / prior PRs:** W-2 cppcheck + W-3 iwyu + W-format (425 repo-wide) + W-tidy (75 diagnostics) — pre-existing carry-forwards per `[[project_011_tls_policy_closed]]` + 011 PR #84. Repo-wide cleanup tracked as a separate `chore/` follow-on branch off post-012 main.
>
> **011 F-1 closure landed.** Phase 7 `tests/transport/test_load_credentials_seam13_witness.cpp` + Gate B verify record satisfy SC-008 binding. 011's deferred F-1 row flipped at 012 Gate B convergence.
>
> **Cross-cuts forwarded.** T-039 / T-040 wiring half SHIPPED; full rows pending session-Phase-4 (CompID-to-TLS-identity binding + 2j `ReloadCertSource` control-plane). T-041 waits for session-Phase-4.

---

## 011-tls-policy — Active feature (2026-05-24, /speckit-implement Phases 1-6)

> Establishes the new `include/fixpp/tls/` + `src/tls/` module from scratch. Ships catalogue rows T-006 / T-007 / T-008 / T-011 / T-013 as `implementing` (await 2h-transport for `done`). T-039 / T-040 / T-041 stay `backlog` with explicit C++ surface-contract forwarding notes (see `feature-catalogue.md`).
>
> **C++ surface published.** Five public types under `fixpp::tls::`:
> - `cert_source` (abstract; 2 pure-virtuals — `load_credentials` awaitable + `load_trust_anchors`) + `file_cert_source` (PEM/DER default impl with encrypted-PEM passphrase support) + `make_file_cert_source(Config, mr) -> expected_t<shared_ptr<cert_source>>` factory.
> - `Pinset` (mid-session-mutable, `shared_mutex` writer + lock-free `atomic<shared_ptr<const pin_snapshot>>` reader per `[2g §6.2]` / §6.5.2) + `make_pinset(Config, mr) -> expected_t<shared_ptr<Pinset>>` factory.
> - `CipherPolicy` (compile-time allow-list per `[const §XII.3]` / `[2g §4.4]` — 3 TLS-1.3 suites + 6 TLS-1.2 suites + 3 kx_groups + 4 sig_algs + 12 banned_tokens; `static_assert(!any_banned(...))`; runtime `is_allowed(string_view) constexpr noexcept` for 2i C-ABI).
> - `SecurityProfile` (4 enumerators incl. `unset = 0` sentinel + `one_way_ca [[deprecated]] = 3`) + `SslCtxConfig` (carries `pinset_snapshot` per NEW-P1-1 BINDING CONTRACT — verify_peer scans the captured snapshot, never calls `cfg.pinset->find/contains`) + `make_ssl_ctx_config(profile, cs, clock, pinset=nullptr, mr=nullptr) -> expected_t<SslCtxConfig>` factory + `verify_peer(SslCtxConfig const&, span<const Certificate>) noexcept -> expected_t<peer_identity>` predicate (10-step short-circuit per `[2g §6.5.1]` / FR-020a).
> - `Certificate` + `peer_identity` value types (view + owning; `[[clang::lifetimebound]]` at every view accessor's declaration site per `[arch §5.5]` + `[2b §6.4]` precedent).
>
> **Error envelope.** 16 new `error::tls_*` variants at slots 78..93 per `[2g §6.6]` + `/clarify` Q2 amendment (`tls_pin_empty_at_open`). C-ABI coalescing groups owned by 2i: `FIXPP_ERR_TLS_CONFIG` (5 variants) / `FIXPP_ERR_TLS_PINSET` (3) / `FIXPP_ERR_TLS_RUNTIME` (1) / `FIXPP_ERR_TLS_HANDSHAKE` (5 — grouping variant `tls_handshake_failed` + 4 DoS-cap / pinning variants) / `FIXPP_ERR_CANCELLED` (1 — reused for `tls_load_cancelled`).
>
> **Test surface.** 21 new test binaries (14 named in `tests/tls/*` + 2 fuzz/conformance + 5 negative-compile / cancellation / per-counterparty / lifetimebound / pmr-fail witnesses) + 3 bench binaries — all green under Clang Debug + ASan + UBSan + TSan + GCC Release. Dual-gate alloc (`counting_resource` + `mallocnesia` LD_PRELOAD) on `Pinset::find` / `snapshot` / `verify_peer` per `[[feedback_tracking_pmr_resource_false_pass]]`.
>
> **Cross-cuts forwarded.** T-039 / T-040 wait for 2h-transport (handshake wiring) to flip from `backlog` → `implementing` → `done`. T-041 waits for session/ Phase-4 (CompID-to-TLS-identity binding) to consume `peer_identity` on `SessionEvent`. The deferred session-recovery feature's catalogue row 400 is NOT touched by 011.
>
> **Pending Phase 6 close-out (this slice).** T051 `/simplify` and T053 `/speckit-verify` (Tier-1 mirror) remain to run per `[[feedback_speckit_simplify_before_verify]]` ordering — `/simplify` first, then `/speckit-verify`, then `/gate-b`.

---

## FIX Latest — MsgType entries only (FIX-Latest)

Scope: confirm A-035–A-065 account for all new MsgTypes in FIX Latest not present in FIX 5.0SP2. EP-level field additions to existing messages are a post-1.0 gap (see Post-1.0 Gap Registry).

| MsgType | Message | Catalogue ID | Gap note |
|---|---|---|---|
| CF | PartyDetailsListRequest | A-043 | — |
| CG | PartyDetailsListReport | A-044 | — |
| CK | PartyDetailsListUpdateReport | A-045 | — |
| CL | PartyRiskLimitsRequest | A-053 | — |
| CM | PartyRiskLimitsReport | A-054 | — |
| CN | SecurityMassStatusRequest | A-063 | — |
| CO | SecurityMassStatus | A-064 | — |
| CR | PartyRiskLimitsUpdateReport | A-055 | — |
| CS | PartyRiskLimitsDefinitionRequest | A-056 | — |
| CT | PartyRiskLimitsDefinitionRequestAck | A-057 | — |
| CU | PartyEntitlementsRequest | A-048 | — |
| CV | PartyEntitlementsReport | A-049 | — |
| CW | QuoteAck | A-065 | — |
| CX | PartyDetailsDefinitionRequest | A-046 | — |
| CY | PartyDetailsDefinitionRequestAck | A-047 | — |
| CZ | PartyEntitlementsUpdateReport | A-050 | — |
| DA | PartyEntitlementsDefinitionRequest | A-051 | — |
| DB | PartyEntitlementsDefinitionRequestAck | A-052 | — |
| DC | TradeMatchReport | A-039 | — |
| DE | PartyRiskLimitsReportAck | A-058 | — |
| DF | PartyRiskLimitCheckRequest | A-061 | — |
| DG | PartyRiskLimitCheckRequestAck | A-062 | — |
| DH | PartyActionRequest | A-059 | — |
| DI | PartyActionReport | A-060 | — |
| DJ | MassOrder | A-037 | — |
| DK | MassOrderAck | A-038 | — |
| DO | MarketDataStatisticsRequest | A-041 | — |
| DP | MarketDataStatisticsReport | A-042 | — |
| DR | MarketDataReport | A-040 | — |
| DS | CrossRequest | A-035 | — |
| DT | CrossRequestAck | A-036 | — |

**Conclusion:** All 31 FIX Latest new MsgTypes (A-035–A-065) are accounted for. No FIX-Latest MsgType gaps.

---

## Gap Summary

All gaps identified during Phase 1.6 top-down pass, with resolution.

| Gap | Section | Catalogue resolution |
|---|---|---|
| NoMsgTypes in Logon — no row for §4.3.8 Specifying supported message types | FIX-SL §4.3.8 | **Added S-037** (see catalogue) |
| Application system identification in Logon — no row for §4.3.9 | FIX-SL §4.3.9 | **Added S-038** (see catalogue) |
| Certificate parameters — no row for FIXS §3.4 | FIXS §3.4 | **Added T-039** (see catalogue) |
| Secrets management / distribution — no row for FIXS §4.1–§4.2 | FIXS §4.1, §4.2 | **Added T-040** (see catalogue) |
| Authorization linked to authentication — no row for FIXS §4.4 | FIXS §4.4 | **Added T-041** (see catalogue) |
| FIX-SL §5.3.6 Extension pack precedence | FIX-SL §5.3.6 | dropped(post-1.0: EP precedence rules; no v1.0 implementation) |
| FIX-SL §5.3.7 Custom application version | FIX-SL §5.3.7 | dropped(post-1.0: custom app versions) |
| FIX-SL §7 Attachment group for alt-encoded messages | FIX-SL §7.1 | dropped(post-1.0: SOFH framing tracked in W-016; attachment group not in v1.0 scope) |
| FIXS §3.6 Application-specific TLS (ALPN/SNI) | FIXS §3.6 | dropped(post-1.0: ALPN/SNI hooks) |
| A-018 / A-024 duplicate (both = ExecutionAcknowledgement BN) | App catalogue | **Dropped A-024**: Status=dropped(duplicate: same as A-018) |

---

## Catalogue ID supplemental notes

Notes that supplement specific catalogue rows (`feature-catalogue.md`) without rewriting the row text. These record dispositions that emerged from Phase 2 design decisions and provide the bidirectional-traceability anchor per `[const §VI.4]`.

**D-008 supplemental:** Codegen scope for v1.0 = FIX 4.2, FIX 4.4, FIX 5.0 SP2, FIXT.1.1. Runtime-XML-only scope = FIX 4.0, FIX 4.1, FIX 4.3, FIX 5.0, FIX 5.0 SP1. The row title in `feature-catalogue.md` covers the broader 4.0–5.0 SP2 surface; codegen vs runtime-XML disposition lives here, in the coverage index. Per `[2c §1.3]` and `[2c Appendix A]`. Source: 2c v1.3 sign-off (2026-05-08); see `[2c Appendix D §2]`.

**NFR-015 supplemental:** Pluggable Clock interface — `fixpp::core::Clock` (4 pure-virtual methods: `now`, `steady_now`, `sleep_until`, `cancel_sleeps`) carried by `EngineConfig`. Source spec sections: `[arch §1.1] Goals` (pluggable clocks promise) and `[2d §4.1] fixpp::core::Clock — interface, lifetime, threading`. Default impl `fixpp::core::system_clock_source` per `[2d §4.2]` (per-session reusable `steady_timer` slot keyed by `Session*` from `session_arena`); test impl `fixpp::core::mock_clock` per `[2d §4.3]` (pimpl per `[const §XI.3]`). The `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock` rule (per `[2d §7.9]`) routes heartbeat (S-003 / S-004), SendingTime, S-035 session scheduling, and session-scoped LOG/OBS records through the per-session clock; engine-scope LOG/OBS records read `EngineConfig::clock` directly and carry a `clock_scope = engine` discriminator. NFR-015 covers the **clock seam only**; the consuming-row owners (the session-module Phase-4 spec for S-003/S-004/S-035, **2k** for LOG-001..004 + OBS-001..003) discharge their own rows. Source: 2d v0.4 sign-off (2026-05-08); see `[2d §11]` drop-in language and `[2d Appendix A]`.

**NFR-016 supplemental:** Awaitable mutex `fixpp::sync::async_mutex` — own implementation per `[SYN §3.2 Q6b]` (BSL-1.0 algorithm attribution to avast/asio-mutex; cppcoro / Lewis-Baker `std::atomic<uintptr_t>` state with three-state `not_locked`/`locked_no_waiters`/pointer-to-LIFO encoding + mutex-owned `next_drain_head_` residual FIFO chain; per-waiter three-state `std::atomic<waiter_phase>` machine `{ queued, granted, cancelled }` arbitrating unlock/cancel CAS with WINNER-ONLY post-CAS `*result_` writes per v1.4 CAS-then-publish). Source spec sections: `[arch §1.1] Goals` (concurrency primitives promise) and `[2f §4.1] fixpp::sync::async_mutex class — public surface`. The six-item design list per `[SYN §3.2 Q6b]` is delivered via `[2f §4.2]` (waiter embedded in awaiter inside the caller's coroutine frame), `[2f §4.3]` (PMR-aware fallback via explicit `async_lock(mr)` overload + session-side helper `async_lock_via_session_executor`), `[2f §4.5]` (ASIO `cancellation_type::total` honoured via per-waiter `phase_` CAS to `cancelled`; awaitable completes with `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary, mapped to `FIXPP_ERR_CANCELLED` at the C ABI per `[2d §6.7]`), `[2f §4.6]` (per-mutex `dispatch`/`post` completion policy; E-3 errata: waiter resumption always uses `asio::post` — never `dispatch` — so the completion always occurs on a freshly-posted task, eliminating the pre-E-3 `running_in_this_thread()` predicate for resumption scheduling; `dispatch` is retained for the no-waiter uncontended grant path only), `[2f §4.7]` (`std::terminate()` precondition on destruction + explicit mutex-owned `cancel_and_drain()` drain primitive with lazy `std::atomic<std::shared_ptr<detail::drain_latch_state>> drain_latch_ptr_` — on the abort path the latch stays published with `aborted_==true` per I-5/I-7 (E-4 / gate-b/r1 fix) so reentrant callers subscribe and observe the abort outcome rather than false success; on the release path `drain_latch_ptr_` is cleared after `signal_release()` per I-7; published before `draining_` per v1.4 / I-1; `signal_release()` + `signal_abort()` + `notify()` per v1.5 / I-7 / I-8; E-4: `cancellation_slot` has no allocator-binding hook — per-thread recycler owns the slot-closure and drives zero-alloc steady-state), and `[2f §9]` test seams (≥ 32 covering FIFO fairness across drain cycles, cancellation mid-wait, destructor-with-waiters, contention stress, TSan + ASan clean, plus the v1.4 / v1.5 seams covering CAS-then-publish arbitration, deterministic latch publication, subscriber-wake-on-reaper-abort, `notify()` non-terminal wake, reentrant-drain-after-abort false-success regression lock, and in-flight-acquirer/reentrant-drain UAF window). Locked executor-compat surface per `[2d §7.4]` (completion on awaiter's bound executor; honours `cancellation_type::total`; always-post resumption per E-3); cross-doc amendments to `[2d §4.5]` (engine-internal `Session::session_arena()` accessor), `[2d §4.7]` (per-mode effect-table row + paragraph contract on `expected_t::unexpected{sync_lock_aborted}` cancellation outcome), and `[2d §7.4]` (locked surface bullet rewording) applied at sign-off per `[2f Appendix D §D.1–§D.3]`. Direct consumers: `MessageStore` writer mutex per `[2e §6.4]` (the named hard hand-off gate from `[2e §3.1]` last bullet), pinset rotation per `[2g]`, seqnum counter per Phase-4 session-module spec. CI enforcement of `[const §XV.9]` `std::mutex`-in-coroutine-context ban via `tools/check_no_std_mutex_in_awaitable_headers.sh` grep gate (clang-tidy custom check is post-v1). NFR-016 is the **primitive seam**; no other catalogue rows discharge through it (the consuming-row owners discharge their own rows). Source: 2f v1.6 sign-off (errata E-1..E-4; gate-b/r1 F-2/F-4 fix 2026-05-19); see `[2f §11]` drop-in language and `[2f Appendix A]`.

**CA-002 supplemental:** `fixpp_error_t` numeric-block layout per `[2i §4.3]` v0.2: cross-cutting block `[0, 99]` (2i-owned, 11 codes — `FIXPP_ERR_OK` / `_CANCELLED` / `_UNKNOWN` per `[arch §5.3]` plus 8 2i-introduced variants `_NULL_HANDLE` through `_CAPI_CONFIG_INVALID`); WIRE `[100, 199]` (2b-owned, 13 occupied per `[2b §6.7]`); DICT `[200, 299]` (2c-owned, 20 occupied per `[2c §6.7]`); THREAD `[300, 399]` (2d-owned, 9 occupied per `[2d §6.7]` — count includes `dispatch_aborted` which still maps to `FIXPP_ERR_CANCELLED` at the C ABI per `[2i §4.9]`); STORE `[400, 499]` (2e-owned, 10 occupied per `[2e §6.7]`); SYNC `[500, 599]` (2f-owned, 4 occupied per `[2f §6.5]`); TLS `[600, 699]` (2g-owned, 15 occupied per `[2g §6.6]`); TRANSPORT `[700, 799]` (2h-owned, 22 occupied per `[2h §6.6]`); DECIMAL `[800, 899]` (2a-owned, 4 occupied per `[2a §7.4]`); reserved `[900, 1399]` for 2j / 2k / 2l / 2m / post-v1 growth; `[1400+]` reserved for future expansion. Live total of prior-doc variants = 4 + 13 + 20 + 9 + 10 + 4 + 15 + 22 = 97. Stability rule per `[SYN §3.5 #19]` / `[const §X.4]`: once a numeric value is published in a tagged C ABI release, it never changes meaning. Audit trail via `tools/abi_history/error_codes_v1.txt` (append-only); CI verifies no re-definitions. Occupancy drift gate `tools/check_capi_occupancy.sh` mechanically counts sibling `[2X §6.X]` rows and asserts the published counts match. Per-block growth is a domain-doc amendment; cross-block growth is a 2i amendment per `[const §XX]`. Source: 2i v0.3 (2026-05-09); see `[2i §4.3]` numeric-block table and `[2i §4.4]` `fixpp_strerror()` lookup discipline.

**SVC-005 supplemental:** Pluggable control-plane interface — `fixpp::service::ControlPlane` (3 pure-virtual methods: `start`, `stop`, `health`; under the `[const §XIV.2]` ≤ 5 cap with 2 slots reserved for v1.x `RotateAuthToken` / `RemapRpcs` per `[2j §10]` Q5). Source spec sections: `[arch §4.11] service` (the surface inventory) and `[2j §4.1] fixpp::service::ControlPlane — abstract interface`. Default impl `fixpp::service::grpc_control_plane` per `[2j §4.6]` (Unix domain socket on Linux / named pipe on Windows; TCP opt-in per `[arch §8.1]`). The proto schema `service/proto/fixpp_control.proto` per `[2j §4.7]` is on the `[arch §9.3]` "Stable from v1.0" tier; proto-evolution rules pinned in `[2j §4.7.1]` (additive-only expansion via MINOR bumps; removals are MAJOR breaks). v1.0 RPC surface: `OpenSession`, `CloseSession`, `Configure` (reserved-empty per `[2j §4.7.1]` additive expansion path), `StreamMetrics`, `StreamLogs`, `StreamSessionEvents`, `Health` (gRPC standard health-check). `RotatePinset` and `ReloadCertSource` are deferred to v1.x per `[2j §10]` Q1 + Q9 (the v1.0 cross-doc state has no AGPL-legal path: `[2i §2]` non-goal #6 declines the C-ABI rotation surface; `service/grpc/*.cpp` cannot include `<fixpp/tls/...>` per `[arch §8]`). AGPL-boundary structural enforcement per `[2j §4.4]` / `[2j §4.6]` + `tools/check_layers.py` lint per `[arch §8]` enforcement bullet (first-landing tracked at `[2j §10]` Q10). Stream backpressure: close-on-overflow with `control_plane_stream_overflow` per `[2j §4.8]` / `[2j §6.4]` (consistent with `[const §XV.15]` no-drop-oldest-on-app-paths; `[const §XIII.2]` permits but does not require drop-oldest on observability paths — v1.0 picks close-on-overflow for visibility). The proto-stability audit-trail file `tools/abi_history/proto_v1.txt` (NEW at 2j sign-off per `[2j App D §D.3]`) mirrors the `tools/abi_history/error_codes_v1.txt` precedent. Source: 2j v0.3 (2026-05-09); see `[2j §11]` drop-in language and `[2j Appendix A]`.

**PY-bindings supplemental:** Python `fixpp` package — SWIG-generated CPython extension wrapping the C ABI per `[arch §4.12]` / `[arch §8]` AGPL boundary; consumes only `<fix/c_api.h>` (no engine-internal C++ headers per `[arch §9.1]`). Source spec sections: `[arch §1.1] Goals` (Python wheel mandatory) and `[2m §1] Goals` + `[2m §4.1–§4.7] Public Python API surface` + `[2m §6.1–§6.7] Behavioral contract` + `[2m §11] Hand-off`. PY-001 (SWIG / `import fixpp`): `[2m §4.1]` package layout + `[2m §4.2]` Engine + `[2m §4.3]` Session + `[2m §4.4]` Message + `[2m §4.5]` Application + `[2m §5]` wrapped C-ABI symbols. PY-002 (GIL discipline): `[2m §6.1]` per-call release/acquire + `[2m §1.3]` rule (4) GIL-protected session-local strand markers + `[2m §6.5]` reentrancy carve-outs. PY-003 (exception translation): `[2m §4.6]` `FixppError` block-mapped hierarchy + `[2m §6.3]` translation boundaries + `[2m §6.7]` 5 new `FIXPP_ERR_BINDING_*` variants in `[2i §1.1]` `[1200, 1299]` (`PYTHON_CALLBACK_RAISED = 1200`, `SUBINTERPRETER = 1201`, `OBJECT_LIFETIME = 1202`, `WHEEL_ABI_MISMATCH = 1203`, `CALLBACK_REENTRANT_CLOSE = 1204` per `[2m App D §D.1, §D.3]`). PY-004 (lifetime / ownership): `[2m §6.2]` Python objects don't outlive native sessions per `[const §X.5]` opaque-handle uniform-destroy + `[2m §6.7]` `OBJECT_LIFETIME` (1202) enforcement. PY-005 (manylinux wheel): `[2m §1.1]` platform matrix (CPython 3.10–3.13 single-interpreter, manylinux 2_28, x86_64) + `[2m §11]` Hand-off CI workflow per `[arch §7.1]` mandatory wheel name `fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl` (single stable-ABI wheel covering CPython 3.10–3.13+) + `cibuildwheel` + `auditwheel repair` per `[const §IV.3]`. v1.0 binding consumption surface is **C-ABI-only** per `[arch §8]` structural enforcement; `tools/check_layers.py` lint extended at 2m sign-off to scan `bindings/python/` for any `#include <fixpp/X/...>` violation (mirrors the 2j precedent at `[arch §8]` enforcement bullet). 2m amends `[2j §11]` hand-off via `[2m App D §D.4]` to declare `fixpp_session_post` as the v1.0 strand-post primitive owed to 2m for outbound `Message.__init__`. Source: 2m v0.3 sign-off (2026-05-10); see `[2m §11]` drop-in language and `[2m Appendix A]`.

**PY-001 DELIVERED — THIN slice (053-python-thin-binding, status `done`, Gate B pending):** `specs/053-python-thin-binding/` ships the first real C-ABI consumer / `0→1`-freeze validator as a **flat-function** `fixpp.*` surface (not the `[2m §4.2–§4.4]` Pythonic Engine/Session/Message classes — those stay backlog). Delivered: a **selective** SWIG interface (`bindings/python/fixpp.i`) wrapping ~26 in-scope C-ABI functions with per-out-param OUTPUT typemaps, str↔ptr+len / commit→`bytes` / send-`bytes` message typemaps, a 1-arg `engine_create` version-macro wrapper, the `%typemap(out) fixpp_error_t` → `fixpp.Error` bridge, and the GIL trampoline (`PyGILState_Ensure`/`Release` + `Py_INCREF` callable + non-owning borrowed-msg proxy, FR-007/FR-013/FR-014); static-linked `_fixpp.so` (`fixpp_capi` PIC archive + `-static-libstdc++/-libgcc`, FR-010/D-6). Tests: `bindings/python/tests/test_roundtrip.py` (two-engine FIX 4.4 loopback round-trip on MsgType `D`/ClOrdID(11), SC-001/SC-003; bad-dict-path → `fixpp.Error`, SC-005) + `test_smoke.py`. SC-004 = ASan **and** TSan CI legs in the Tier-1 `python-bindings` job (UBSan omitted — CPython C-API aliasing noise; waiver in the verify doc). **No `include/fix/c_api.h` change — the `0→1` GA freeze stays HELD** (FR-012). PY-002 (full GIL) / PY-003 (typed exceptions) / PY-004 (lifetime hardening) / PY-005 (wheel) remain backlog.

**PY-002 + PY-003 DELIVERED (054-python-gil-exceptions, status `done`, Gate B pending):** `specs/054-python-gil-exceptions/` hardens the 053 binding along two SWIG-layer axes, **no `c_api.h` change (0→1 freeze HELD)**. **PY-002 (GIL discipline, `[2m §6.1, §6.5]`):** an exhaustive GIL-discipline audit table + bound-trampoline census (exactly one, `fixpp_py_recv_trampoline`) in `fixpp.i` (FR-001/003); the three blocking wrappers (`session_close`/`session_send`/`engine_destroy`) release the GIL via macro-guarded `%exception` bands (FR-002); a discriminating local-only `FIXPP_PY_GIL_RELEASE_CANARY` (proven RED 5/5 hang; normal GREEN 5/5) + a subprocess-watchdog pinning the 053 raising-callback fix (FR-004/011, SC-003/004); `test_gil_release_canary.py` + `test_callback_raise_watchdog.py` + `_gil_staging.py`. **PY-003 (typed exceptions, `[2m §4.6, §6.3, §6.7]`):** the `[2m §4.6]` hierarchy verbatim (root `FixppError` + one subclass per `fixpp_error_t` block + 5 `BindingError` subclasses) + **`AppError`** for the post-2m `[1400,1499]` block (`[2i §4.3]`/051, no new code); `Error = FixppError` alias; single exposed translator `fixpp._map_to_class`/`exception_for_code` the out-typemap routes through (FR-006/007/008); unmapped→root `FixppError`, in-typemap conversion failures→root message-only (FR-009/010); header-sourced set-equality coverage over the 47 `error.h` codes (SC-002); `test_exceptions.py` + `test_error_coverage.py`. The normative `[2m]` is amended (Article XX, in-PR) at all four send-from-callback sites → **L-054-1** (as-built blocking `session_send`-from-callback deadlocks; documentary, active detection deferred to PY-004). Tier-1 `python-bindings` none/asan/tsan all green with the new tests (UBSan leg still waived, L-054-2 / 053 D-9). PY-004 (lifetime hardening) / PY-005 (wheel) remain backlog.

**PY-004 DONE (055-python-lifetime-ownership, status `done`, MERGED 2026-06-30 PR #157 squash `f930bcd`, gate-a-done + gate-b-done, 0 Gate-B waivers):** `specs/055-python-lifetime-ownership/` delivers the full OO lifetime/ownership layer over the frozen C ABI, **no `c_api.h` change (0→1 freeze HELD)**. Surface: `fixpp.Engine` / `Session` / `Message` / `Application` / `Dictionary` wrappers re-exported from the package (FR-001), each with Python-side liveness sentinels (`_dead`) that raise `fixpp.ObjectLifetime` (1202) before any stale-handle C-ABI entry (FR-002/003/016). Inbound callback flyweights are invalidated at callback return, closing **L-053-1** (FR-004/017); teardown is ordered/idempotent, releases the binding-owned callback ref, and adds context-manager + GC-warning behavior (FR-007..011). The callback path now actively rejects reentrant `send` / `session.close` / `engine.close` with `fixpp.CallbackReentrantClose` (1204), implementing the `[2m]` Article XX send/close inversion on the OO path (FR-017 / SC-007). Handle-bearing wrappers refuse pickle (`TypeError`, FR-013), and the `Engine` constructor retains the sub-interpreter rejection goal with a tolerated CPython 3.12 import barrier witness (FR-018). Tests: `bindings/python/tests/test_lifetime.py`, `test_close_flow.py`, `test_callback_lifetime.py`, `test_context_manager.py`, `test_reentrancy.py`, `test_pickle_ban.py`, `test_subinterpreter.py`. Merged via PR #157 (squash `f930bcd`); Gate B (2 rounds) added a `try/finally` error-path-teardown fix (P1-a) + a Session-wrapper weakref witness (P2). 81 tests GREEN none/asan/tsan (SC-006); Tier 1/2/3 all green on the merge head.

---

## 029-persistent-seqnum-hydrate — MERGED (PR #111 squash `0b9c8b8`, 2026-06-09; gate-a-done + gate-b-done)

> Closes "T034" — the inbound store-persistence gap. Ships catalogue row **S-042 → `done`** (FIX 4.4: durable inbound counter + bidirectional hydrate-on-open). Discharges the `008`-boundary prerequisite for S-018 (RefreshOnLogon); S-018 shipped via **025** (PR #112 squash `357f5ab`, MERGED 2026-06-10).
>
> **Source units covered.**
> - `SeqnumManager::hydrate(next_inbound, next_outbound)` — new production awaitable setter; loads both counters from the persisted store into the in-memory manager at cold open.
> - `Session::ensure_hydrated_()` — one-shot cold-open helper; reads both `next_seqnum(dir,false)` from the persistent store before the first counter touch (both roles, both direct + engine-managed paths); Logon-gate-aware inbound seed (withheld on `141=Y` / `reset_on_logon`); latched-after-success (D-9); non-persistent skip via `store_is_persistent_`.
> - `Session::persist_inbound_advance_()` — per-delivery durable inbound write; invoked at every `check_inbound`-success site (site-keyed disposition matrix); fatal-disconnect on failure.
> - `MessageStoreFactory::yields_persistent_store()` — non-pure discriminator accessor (default `true`; `MemoryStoreFactory` → `false`) captured at `open()` into `store_is_persistent_`.
>
> **Test files.**
> - `tests/session/test_persistent_seqnum_hydrate.cpp` — W1–W14 witnesses (outbound resume, inbound durable track + resume, deliver-then-persist ordering, both-direction acceptor cold resume, post-GapFill lower bound, inbound persist failure fatal, non-persistent no-op, one-shot + happens-before, reset-wins, seed-withheld-on-141, hydrated 789 advertisement, validate-off 35=4 persist split, custom-store discriminator, hydrate read-failure fatal, no-heap under mallocnesia).
> - `tests/interop/happy/hp_fix44_restart_resume_test.cpp` — live both-role restart-resume cell (skip-without-counterparty; assertions (a)–(d): Active reached, outbound resumed > 1, inbound resumed > 1, no fatal).
>
> **Normative refs.** `[FIX-SL §4.1]` (sequence numbers); `[FIX-SL §4.3.12]` (synchronization after logon); `[FIX-SL §4.8.x]` (ResendRequest / SequenceReset recovery — at-least-once restart via INV-H1 lower bound). No new wire field, error slot, codegen, or C-ABI surface.
>
> **Exact-set diff** `[const §VI.4]` — source units ↔ test files:
>
> | Source unit | Test coverage |
> |---|---|
> | `SeqnumManager::hydrate` | W1 (outbound resume), W4 (acceptor cold resume both directions), W8/W8-hb (one-shot + happens-before), W9a (reset wins), W9b (seed withheld on 141), W11 (hydrated 789), W14 (read-failure fatal) |
> | `ensure_hydrated_()` (outbound path) | W1, W8, W9a, W13 (custom-store discriminator), W14 |
> | `ensure_hydrated_()` (inbound seed + Logon-gate-aware withheld) | W4, W9b, W11 |
> | `persist_inbound_advance_()` (PERSIST sites) | W2 (durable track + resume), W3 (deliver-then-persist ordering), W6 (persist failure fatal), W12 (validate-off 35=4 split) |
> | `yields_persistent_store()` / `store_is_persistent_` | W7 (non-persistent no-op — memory + null), W13 (custom-store discriminator) |
> | Full interop path | `hp_fix44_restart_resume_test.cpp` (W10 — both-role live restart-resume) |

---

## Post-1.0 Gap Registry

Items that are normative in the spec but explicitly deferred from fixpp v1.0. These do NOT block Phase 2 but must be visible for future planning.

| Item | Spec section | Reason deferred | Target version |
|---|---|---|---|
| EP-level field additions to existing FIX 4.x/5.x messages (Extension Packs EP001 onward) | FIX-SL §5.3.6; FIX Latest EPs | FIX Latest EP fields in existing messages require dynamic dictionary extension beyond v1.0 scope; FIX Latest new MsgTypes (A-035–A-065) are already tracked | v1.2 (FIX Latest app messages milestone) |
| FIXT custom application version identifiers | FIX-SL §5.3.7 | Non-standard; negligible demand vs complexity | post-v1.2 |
| FIX-SL §7 Attachment group (alt-encoded payloads over FIX envelope) | FIX-SL §7 | SOFH framing (W-016) is the practical path; attachment group is legacy | post-v1.1 (SOFH milestone) |
| FIXS §3.6 Application-specific TLS (ALPN / SNI extensions) | FIXS §3.6 | Operational/infrastructure concern; not required by any known FIX venue for v1.0 | v1.3 (if requested) |
| LFIXT succinct mode interoperability (S-028) | FIX-SL §5.4.5 | Non-interoperable with standard FIXT; no known production demand for v1.0 | v1.x (on demand) |
| FIX Orchestra / Rules of Engagement machine-readable format (D-011) | FIX Orchestra spec | Future direction; QuickFIX XML sufficient for v1.0 | v1.2+ |
| FIXP binary session layer framing (W-015) | FIXP spec | Separate protocol; addressed in v1.4 | v1.4 |
| SBE (Simple Binary Encoding) wire format (OSS-012, OSS-013) | Aeron SBE spec | Separate encoding; addressed in v1.3 | v1.3 |
| FAST encoding (OSS-011) | OpenFAST spec | Separate encoding; addressed in v1.5 | v1.5 |

## 023-engine-session-strand — Implemented (branch `023-engine-session-strand`; /speckit-verify GREEN; pre-Gate-B)

> Two serialization domains make a multi-threaded `io_context` genuinely supported (L-019-3 LIFTED). **No new module, no new error slot, no C-ABI.** Production surface (exact set): `src/session/engine.cpp` + `include/fixpp/session/engine.hpp` (the per-session strand + control strand + D-SNAP snapshot + bounded-handle lease + `stop()`-on-control-strand teardown), `src/session/session_executor.cpp` + `include/fixpp/core/session_executor.hpp` (the D3-B `adopt_strand_t` overload), `src/session/session.cpp` open() adopt seam, `include/fixpp/session/session_config.hpp` (`engine_adopt_strand` field). The single recorded **C++ ABI change**: `Engine::lookup() : Session* → std::shared_ptr<Session>` (FR-008/SC-004) — a bounded handle (debug-lease-asserted).
>
> **Test surface.** `tests/session/test_engine_session_strand.cpp` (witnesses V-1/V-3/V-8/V-9/V-10/V-11/V-12 + V4V5 ABI/baseline cell), V-2 reuses `business_messages_roundtrip` (`SendFromInsideFromApp_NoDeadlockNoUAF` — the BIO_ctrl acceptance, now stable ×10 TSan). The lookup() ABI change rippled mechanically (`Session*`→`auto`, `.get()` at raw call sites) across ~20 session + interop test files (no logic change — V-4 no-rewrites preserved).
>
> **Coverage (exact-set, touched files).** 023-introduced functions ~100% line: `publish_reader_snapshot_unlocked_` 100/100, `register_session` 100/100, `lookup` 100 line, `acceptor_bound_endpoint` 100 line, `make_session_executor(adopt_strand_t)` 100, `LeasedHandle::~LeasedHandle` 100; `publish_entry` 75 (stopped-disposition defensive edge). Whole-file engine.cpp 88.6% line / 69.8% branch — PASS-with-justification ([const §IX.1] Article IX §1): deficit is pre-existing untouched accept/connect-loop defensive error branches (015 engine.cpp precedent). §4.x rows unchanged (no new conformance/application rows; this is concurrency wiring).
>
> **/speckit-verify GREEN** (on `0527a11`): static analysis clean (clang-format + clang-tidy remediated in-run; cppcheck 023-scope clean; check_layers OK; §XV.9 `check_no_std_mutex_corpus` GREEN); 6-preset build matrix all PASS; TSan FULL suite **388/388** (V-8 + V-11 GREEN via D-SNAP); ASan/UBSan witness sets clean; debug broad 94/94; coverage 77/77. ABI/alloc/fuzz/bench N/A or SKIPPED-with-rationale (engine-internal; V-6 `is_lock_free()==false` recorded, send micro-bench → L-023-2; abidiff static-archive → compile-enforced static_assert + nm baseline). Record: `.specify/decisions/023-engine-session-strand-verify.md` (gitignored). Two in-flight catches: unpublish entry.session-retention fix (3410707); V-8 retarget + `any_executor_base` suppression narrowing (DD-2026-06-06, Codex-validated).

---

---

## 030-received-reset-inbound-advance — Implemented (branch `030-received-reset-inbound-advance`; pre-Gate-B)

> Conformance correction of the received-`141` inbound advance (found via a failed live acceptor interop cell vs QuickFIX-cpp/J). **No new module, error slot, wire field, codegen, or C-ABI.** Production surface (exact set): `src/session/session.cpp` — two arms, each = `store_is_persistent_ ? fatal : logged` reset disposition + a guarded `set_next_inbound(seqnum_min+1)` + `persist_inbound_advance_()` restore: (a) the acceptor `NotConnected` received-`141` arm (`peer_sent_reset && !reset_on_logon`, guarded on `logon_inbound_advanced`); (b) the initiator Logon-ack `peer_ack_sent_reset_flag` arm (consolidated onto the shared `reset_seqnums_to_one_durable()` helper, guarded on `logon_inbound_advanced_init`). Amends S-017/S-031/S-032; see B-030-1/B-030-2.

> **Test surface.** `tests/session/test_reset_on_lifecycle.cpp` — acceptor: `ResetOnLogon_Off_Received141_NextInboundIsTwo`, `Received141_PeerNextMsgSeq2_HarmCheck`, `Received141_AcceptorDiscriminatingTriple` (the discriminating triple: next_inbound==2 ∧ reply.34==1 ∧ reply.789==2), `Received141_PersistentStore_InvH1_StoreEqualsManagerTwo`, `Received141_PersistentStore_ResetFailure_Disconnects_NoOverPersist` (FR-010 soundness — asserts store retains last-good N=37 (persist-to-2 not reached), not store≤manager), `Received141_GuardSkipsWhenNoConsumedReset` (guard-correctness); initiator: `Initiator_Received141Ack_NextInboundTwo_NoResend`, `Initiator_Received141Ack_PersistentStore_StoreEqualsManagerTwo`, `Initiator_Received141Ack_PersistentStore_ResetFailure_Disconnects`; the 024 contract witness **split** into `ResetOnLogon_Off_Received141_StoreFailure_PersistentDisconnects` (FR-010) + `..._NonPersistentStillActive` (retained 024 I-07). Blast-radius pins flipped (**9 pins total = 8 value-pins + 1 contract-witness split**; authoritative count from a clean-build full ctest — incremental builds gave false-greens that masked two): `test_reset_seqnum_policy_matrix.cpp` (3 acceptor + 1 initiator `next_inbound` 1→2), `test_next_expected_msgseqnum.cpp` (`AcceptorReplyReceived141_Advertises2`, 789 1→2), `test_persistent_seqnum_hydrate.cpp` (W9b acceptor `next_inbound`+`durable_inbound` 1→2, AND `INV_H1_Initiator_PeerAck141_NoOverPersist` `durable_inbound` 1→2 — initiator INV-H1 twin), `test_refresh_on_logon.cpp` (`W6_Acceptor_KnobOn_PeerResetLogon_InboundSeedWithheld` `next_inbound` 1→2 AND `call_count` 2→3 — a store-interaction-count pin invisible to a value-grep). The last two (initiator INV-H1 twin + W6) were undercounted in the original 7-pin analysis; the shared received-141 path is driven by tests across 013/024/025/027/029.

> **Coverage — 100% branch on the 030 new code (lcov/llvm-cov BRDA, both arms of every new conditional).** Verified branch counts (coverage preset): acceptor ternary `store_is_persistent_?fatal:logged` (1940) T9/F12; acceptor reset-fail check (1942) T2/F19; acceptor restore guard `logon_inbound_advanced` (1955) T18/F1; initiator ternary (3186) T5/F2; initiator reset-fail check (3188) T1/F5; initiator restore guard `logon_inbound_advanced_init` (3200) T5/F1. Witness mapping: (a) acceptor guard true-arm — discriminating-triple + INV-H1 witnesses; (b) acceptor guard false-arm — `Received141_GuardSkipsWhenNoConsumedReset`; (c) initiator guard true-arm — initiator witnesses; (c') initiator guard false-arm — `Initiator_Received141Ack_GuardSkipsWhenNoConsumedReset` (added during /speckit-verify to close the one branch gap the coverage check found — the symmetric twin of (b)); (d) `store_is_persistent_?fatal:logged` both arms both roles — fatal via fault-injection (T008/T014), logged/non-persistent via the contract-split sibling + pre-existing non-persistent initiator coverage. FR-006/SC-004 non-regression: the `reset_on_logon=true` knob suite + steady-state suites unchanged (full-suite green). **/speckit-verify: 6-preset matrix GREEN** (clean-built each, per-preset, to defeat stale-object false-greens) — debug 435/435 (incl. codegen-cleanliness gate), ASan 434/434, UBSan 434/434, TSan 434/434, coverage 435/435, gcc-release 435/435.

---

## 034-credential-store-redaction — Implemented (branch `034-credential-store-redaction`; pre-Gate-B)

> Security hardening (Fable F-c). Masks the outbound Logon `Password(554)` value **before** it enters the message store, mitigating the cleartext-at-rest exposure (**L-033-6 → B-034-1**); the wire frame is transmitted unmasked so the peer still authenticates. **No new module, error slot, wire field, config knob, codegen, C-ABI, or `MessageStore` surface.** Amends the at-rest half of catalogue row **S-022**; corrects the 033 T024/T020 "no production frame persistence exists" claim (overlooked the 008 store).

> **Production surface (exact set).**
> - `include/fixpp/session/logon_credentials.hpp` — two new inline byte-utilities (siblings of `redact_tag554`, no public ABI surface): `mask_tag554_same_length_inplace(std::span<std::byte>) noexcept` (in-place, same-length `'*'` overwrite of the field-anchored `554` value; zero heap) and `frame_has_genuine_tag554(std::span<const std::byte>) noexcept` (const detection for the maskability gate).
> - `src/session/session.cpp` `Session::store_then_emit` — T006 masking branch: gate `frame_has_genuine_tag554(frame) && scan_frame_header(frame).msg_type=="A"`, copy into a coroutine-frame `std::array<std::byte, kMaxMaskableLogonBytes>`, mask, store the masked span; transmit the original `frame` (Step 2 unchanged). Over-bound (`frame.size() > kMaxMaskableLogonBytes`) → fail-closed skip-store-but-transmit (I-07). T007 open()-time role-independent credential-length guard (extends the 033 FQ-1/FQ-3 validation).
> - `include/fixpp/session/session.hpp` — `static constexpr kMaxMaskableLogonBytes = 256` (= `build_logon` `logon_buf`/`reply_buf` capacity; not a public config/ctor/template param) + a `FIXPP_TEST_HOOKS`-gated `store_then_emit_test_access` accessor (methods-only, no layout change).

> **Test surface.** `tests/session/test_credential_store_redaction.cpp` — 7 masker units (`Masker_SameLength_FieldAnchored_unit` A–G) + 6 integration witnesses: `T005_Persisted_LogonPassword_AbsentFromStoreFile_MaskPresent` (FileStore raw-disk-byte: cleartext absent + same-length mask present), `T005_Acceptor_ReplyLogon_PasswordMaskedInStore`, `T005_InMemoryStore_CredentialedLogon_AlsoMasked`, `T008_Wire_LogonPassword_UnmaskedOnTransmit` (wire carries cleartext, masked form absent on wire), `T009_CredentialFreeLogon_StoredByteIdenticalToWire`, `T009_NonLogon_WithGenuine554_StoredUnchanged` (MsgType=A gate skips a `35=D` carrying `554`); + `T010_OverBound_SmallBoundSeam_SkipStoreButTransmit` (frame-injection via the `FIXPP_TEST_HOOKS` accessor; **mutation-proven discriminating**) and `NoHeap.StorePath_NoNewAllocation` (+ `credential_store_redaction_mallocnesia` ctest).

> **Coverage / NFR.** New byte logic + the `store_then_emit` branch under ASan/UBSan/TSan. The over-bound branch earns its BRDA via the T010 frame-injection seam (mechanism deviation from the design's `FIXPP_TEST_LOGON_MASK_BOUND` override, which could not reach `store_then_emit` compiled without the test define — see `plan.md ## Gate A`). **SC-004 (no added allocation): primary evidence is zero-alloc BY CONSTRUCTION** (`std::array` + `std::memcpy` + in-place byte overwrite — no allocating operation); the mallocnesia gate is present per the established 027 `NoHeap.*` pattern but is **inert in `linux-clang-debug`** (project-wide weak-symbol non-interposition — filed in `REMAINING-WORK.md` item 13). `/speckit-verify` matrix pending.

---

## 043-plaintext-tcp-transport — branch `043-plaintext-tcp-transport` (2026-06-17)

> Adds `asio_plain_transport` (plain TCP transport sibling to `asio_tls_transport`) gated behind `SecurityProfile::kind::insecure_plain_tcp` (loud `[[deprecated]]` opt-in, `[const §XII.5]` v0.3 amendment). Catalogue row **T-042**. Also closes the pre-existing inbound `EncryptMethod(98)≠0` gap (S-021; T030). No new wire field / error slot / codegen / C-ABI surface.

> **Production surface (exact set).**
> - `include/fixpp/transport/asio_plain_transport.hpp` + `src/transport/asio_plain_transport.cpp` — `asio_plain_transport` (plain socket; no TLS layer; state `{fresh,connected,closed}`; `close()` = `socket_.close()` with no SSL_shutdown / no `tls_close_timeout` wait)
> - `include/fixpp/transport/transport_factory.hpp` (`transport_security_kind` enum, `kind()` defaulted-virtual on `TransportFactory`, `asio_plain_transport_factory` decl, `make_asio_plain_transport_factory` free function)
> - `src/transport/transport_factory.cpp` (`asio_plain_transport_factory` body, `make_asio_plain_transport_factory`)
> - `include/fixpp/session/security_profile.hpp` (`insecure_plain_tcp` enumerator + `[[deprecated]]` attribute)
> - `include/fixpp/session/reconnect_fsm.hpp` + `src/session/reconnect_fsm.cpp` (`is_plaintext_`, `set_plaintext_profile`, `set_transport_factory`, handshake-skip path)
> - `src/session/session.cpp` (`effective_transport_factory_` member, auto-derive + FR-008 mismatch reject at `open()`, `live_peer_id_` stays `nullopt` for plaintext handoffs)
> - `src/session/engine.cpp` (`run_accept_loop`: ssl_cfg map plaintext arm, post-accept cast/handshake skip, `assert_transport_on_session_strand` plaintext arm)
> - `src/transport/asio_listener.hpp` + `src/transport/asio_listener.cpp` (`transport_kind` in `asio_listener::Config`, `make_asio_plain_transport_factory` per-accept)
> - `src/session/admin_messages.cpp` T030: `interpret_logon` rejects inbound `98≠"0"` (present-but-malformed fails closed)

> **Test surface.**
> - `tests/transport/test_asio_plain_transport.cpp` — SC-001/T005: direct-drive loopback (connect → read/write → close), no TLS ClientHello byte, cancel/post-close paths
> - `tests/transport/test_asio_plain_transport_config.cpp` — SC-008/T006: TCP knob + no-close-notify close path
> - `tests/session/test_session_plaintext_roundtrip.cpp` — SC-001/T007: end-to-end acceptor + initiator Logon/Logout over `run_accept_loop`
> - `tests/session/test_session_plaintext_authz.cpp` — SC-004/T008: auth-inert (no `authorize()` call, `live_peer_id_==nullopt`), `check_comp_id` still rejects mismatch
> - `tests/session/test_insecure_plain_tcp_deprecated.cpp` — SC-005/T017: automated negative-compile `try_compile` harness
> - `tests/session/test_session_open_rejects_unset_security_profile.cpp` — SC-002/T018: `unset` rejected, no-implicit-default
> - `tests/session/test_session_plaintext_factory_mismatch.cpp` — SC-003/T021/T022: effective-factory mismatch matrix + auto-derive reach-mint
> - `tests/session/test_interpret_logon_encrypt_method.cpp` — FR-009/T030: inbound `98≠0` reject (4 cells, mutation-tested: absent/zero-valid/nonzero-reject/malformed-closes)

> **Coverage.** Sanitizer/coverage matrix (`linux-clang-debug`, ASan, UBSan, TSan, coverage, gcc-release) pending T029 run by the orchestrator (`/speckit-verify` step).

---

## 044-toml-session-config — branch `044-toml-session-config` (2026-06-19)

> Native TOML config-file loader (`load_toml_config`) translating a TOML file into a fully-validated `ConfigBundle`. Catalogue row **T-043** (design row, `[const §XV.16]`). No new wire field / error slot / codegen / C-ABI surface (FR-004). Isolated in the `fixpp_config_toml` static-library target — not linked into `fixpp::session` or `fixpp::core`.

> **Production surface (exact set).**
> - `include/fixpp/config/toml_config_loader.hpp` — `load_toml_config(path, LoadOptions) noexcept → LoadResult` entry point + `LoadOptions` struct (engine_executor + load-time memory resource)
> - `include/fixpp/config/config_bundle.hpp` — `ConfigBundle`, `EngineEstablishment`, `SessionDefinition`, `LoadResult = std::expected<ConfigBundle, vector<LoadDiagnostic>>`
> - `include/fixpp/config/load_diagnostic.hpp` — `LoadDiagnostic` (key_path, location, reason_class, message) + `reason_class` enum
> - `src/config/toml_config_loader.cpp` — loader orchestrator (parse → merge_defaults static fn → validate → resolve → accumulate)
> - `src/config/scalar_mappers.cpp` / `src/config/mappers.hpp` — per-key string→typed-field mappers (bucket-A scalars + duration parser)
> - `src/config/selector_resolver.cpp` — object-selector dispatch (`store`, `clock`, `cert_source`, `transport`, `dictionary`, `security_profile` arms)
> - `src/config/loader_internal.cpp` / `src/config/loader_internal.hpp` — noexcept-boundary helpers (`trap_throw_to_expected`, `DiagnosticAccumulator`, `resolve_path`)

> **Coverage note.** This is a **synchronous cold-path** component — no coroutines, no async, no hot-path paths. All branches are exercised by ordinary unit tests. The `fixpp_config_toml` target links into the 5 config test binaries and is not included in any other sanitizer/session/transport build target. The coverage gate (`[const §IX.1]`) applies per-file to `src/config/` and `include/fixpp/config/` — measured in the `linux-clang-coverage` preset at `/speckit-verify`. A **Gate-B blocker (T039)** is outstanding: tomlplusplus 3.4.0 aborts/UBs on certain malformed TOML table headers, bypassing the `noexcept` boundary — reproducer at `tests/config/fuzz/crashes/repro_toml_assert_assume.toml`.

## 045-observability-config (logging leg) — branch `045-observability-config` (2026-06-20)

> Extends the 044 loader to hydrate the existing `fixpp::log::Logger` (file / syslog / OTLP-log sinks) from `[logger]` / `[[logger.sinks]]`. Catalogue row **T-044** (design row, `[const §XV.16]`). No new wire / error / codegen / C-ABI surface (FR-024). Same `fixpp_config_toml` target; new include edge `config → log` + a **conditional** link edge `config → fixpp_log_otlp` (gated by `FIXPP_CONFIG_HAS_OTLP`).
>
> **New/changed source (the coverage surface):**
> - `src/config/logger_resolver.cpp` / `.hpp` (NEW) — `resolve_log_sink` (object-minting + inline side-effect-free preflight: dir stat/access, OTLP cert readable+PEM-magic, endpoint non-empty), `resolve_engine_logger` (composite scalars + ordered `[[logger.sinks]]` → file-scoped `PendingLogger`), `construct_loggers_if_clean` (SOLE side-effectful step, gated on an empty whole-file accumulator).
> - `src/config/toml_config_loader.cpp` — `recognize_keys()` flips `logger` deferred→recognized; per-session `[session.logger]` wiring + the single end-of-load construct call.
> - `src/config/scalar_mappers.cpp` — `map_syslog_facility` (20-name closed POSIX set, build-conditional `LOG_*`), `validate_pow2_capacity`.
> - `src/config/loader_internal.cpp` / `.hpp` — `redact_url_userinfo` (FR-023).
> - `include/fixpp/config/config_bundle.hpp` — one additive `shared_ptr<fixpp::log::Logger>` field.
>
> **Coverage note.** Synchronous cold-path (the constructed `Logger`'s own threads are the inherited 017 contract, not new loader concurrency — see L-045-1). Coverage gate (`[const §IX.1]`, per-file DA/BRDA, `linux-clang-coverage` preset) at `/speckit-verify`. Build-conditional arms (syslog-unavailable `#else`, OTLP-unavailable `#else`, the syslog build-undefined-`LOG_*` arm) are unreachable on a single preset and assessed per the templated-header DA/BRDA basis, not the aggregate. Design choice (per the 044 PATH-B precedent): pure config translation of the existing public log value-types — no new logging machinery.

## 049-c-abi-handles-errors — C ABI Feature A (CA-001..004, 2026-06-23)

> **New/changed source (the coverage surface):**
> - `src/capi/error.cpp` (NEW) — `fixpp_capi::detail::translate()` (total 116-arm switch, no `default`), `translate_for_consumer()` (forward-compat downgrade), `fixpp_strerror()` (static zero-alloc string literals). The 116-arm switch is the coverage risk → driven by the enumerating correctness oracle (`tests/capi/error_surface_test.cpp` against `tests/capi/expected_error_map.csv`, mutation-tested), with explicit override-group (`session_*`/`log_*`/`otel_*`/`app_*`/`out_of_memory` → `UNKNOWN`) assertions.
> - `src/capi/version.cpp` (NEW) — `fixpp_version()` / `fixpp_library_version()` (value-typed PoD, zero-alloc).
> - `include/fix/c_api/{error,version,handles,export}.h` (NEW) — C-clean public headers (typedefs/macros/decls; passive — no executable coverage surface).
> - `src/capi/decimal.cpp` — local `map_error()` replaced by a thin forwarder to the shared `translate()` (renumber lockstep, FR-011).
> - `include/fix/c_api/decimal.h`, `include/fix/c_api.h` — provisional codes removed → `#include error.h`; umbrella aggregates the split headers + drops the stale `FIXPP_C_ABI_VERSION_*` block.
>
> **Coverage note.** Pure cold-path value translation; no concurrency, no allocation. The 116-arm `translate()` totality is `-Wswitch`-enforced; correctness (not just totality) is the checked-in oracle's job. `fixpp_strerror`'s `"unknown error"` default + the override-group arms are exercised. Two new Tier-1 shell gates (`tools/check_capi_occupancy.sh`, `tools/check_capi_reentrancy.sh`) carry their own positive+negative ctest fixtures (`capi_occupancy_negative`, `capi_reentrancy_negative`). Coverage gate (`[const §IX.1]`, per-file DA/BRDA, `linux-clang-coverage` preset) at `/speckit-verify`.

## 050-c-abi-session-send-recv — C ABI Feature B (CA-005..007, 2026-06-24)

> **New/changed source (the coverage surface):**
> - `src/capi/engine.cpp` (NEW) — `fixpp_engine_create` / `fixpp_engine_start` / `fixpp_engine_destroy` (lifecycle thunks) + `CapiApplication` trampoline (onLogon/onLogout/fromApp). Key complexity: shell-leak tombstone for double-destroy idempotency (`tag_` field, `FIXPP_HANDLE_TAG_DEAD`); stop()+join sequencing before tag rewrite; consumer_minor recording and `translate_for_consumer` composition on every fallible return.
> - `src/capi/session.cpp` (NEW) — `fixpp_session_open` / `fixpp_session_close` / `fixpp_session_is_established` / `fixpp_session_send` / `fixpp_session_register_callback`. Key: `check_session` validity guard (atomic `valid.load(acquire)` + `engine_.has_value()` → INVALID_HANDLE); session-strand close via `co_spawn+use_future`; steady-state abort on escaping exception (FR-008/FR-019).
> - `src/capi/config.cpp` (NEW) — engine-config + session-config builder families (heap-allocated opaque wrappers; per-setter eager validation; NULL-safe destroy).
> - `src/capi/capi_internal.hpp` — concrete struct definitions for `fixpp_engine` / `fixpp_session` / `fixpp_engine_config` / `fixpp_session_config` / `fixpp_dict` / `fixpp_msg`; `SessionSlot`; `CapiApplication`.
> - `include/fix/c_api/{engine,session}.h` (NEW) — C11-clean exported surface; reentrancy doc-blocks; version bump MINOR 2→3.
>
> **Coverage note.** Mixed hot-path (send/fromApp) and cold-path (create/start/open) surface. The engine + session lifecycle is exercised by `tests/capi/lifecycle_test.cpp` (happy path + double-destroy no-UAF + post-destroy INVALID_HANDLE + concurrent send/close TSan witness), `lifecycle_negative_test.cpp` (all rejection arms), `send_recv_test.cpp` (two-engine round-trip + ASan dispatch-window + no-callbacks-after-close). Send-path error arms: `error_block_test.cpp` (translate oracle, SC-004 downgrade) + `error_live_test.cpp` (malformed-payload UNKNOWN + no-transmit; seqnum-overflow STORE_RUNTIME + no-transmit; live minor-2 downgrade boundary). Receive-path alloc: `recv_alloc_guard_test.cpp` + mallocnesia gate. Named limitations: L-050-1 (dict/field-accessors Feature C), L-050-3 (store-I/O-on-send I-07 swallow), L-050-4 (session/app block deferred), L-050-y (send-during-engine-destroy quiesce contract). Coverage gate (`[const §IX.1]`, per-file DA/BRDA, `linux-clang-coverage` preset) at `/speckit-verify`; W-1 carried for pre-existing defensive arms.

## 051-c-abi-message-accessors — C ABI Feature C (CA-008/009/010, 2026-06-25)

> **New/changed source (the coverage surface):**
> - `src/capi/message_read.cpp` (NEW) — CA-008 field read accessors + CA-010-read group cursors, thin thunks over `wire::MessageView::get` / `OffsetTable::group_slices`; aliasing views, zero-global-heap, steady-state abort-on-escape. Group cursor (`fixpp_group`) is ARENA-allocated from the parse arena (`OffsetTable::resource()`), not raw `new` (the get_group leak fix).
> - `src/capi/message_write.cpp` (NEW) — CA-009 outbound accumulator (`create_outbound`/`set_*`/`remove_tag`/`commit`/`destroy`/`clone`) + CA-010-write group builder (`msg_group_begin`/`group_builder_add_entry`/`entry_set_*`/`entry_group_begin`/`msg_group_end`). Per-message `monotonic_buffer_resource` (zero-global-heap set_*/commit, SC-003); recursive group serialiser (INV-4); builder/entry are arena-allocated index-holders (stable under vector reallocation); LIFO close + commit-with-open-builder guard.
> - `src/capi/engine.cpp` (EDIT) — `CapiApplication::toApp` override (the send-callback trampoline; verdict→`expected_t`; framed read-only view).
> - `src/capi/session.cpp` (EDIT) — `fixpp_session_register_send_callback`.
> - `src/capi/error.cpp` (EDIT) — `[2i §4.3]` amendment: re-point 5 reachable session/app `translate()` arms to `[1400,1404]`; per-code `introducing_minor` lookup (existing minor 2, six new minor 4); +6 `fixpp_strerror` entries.
> - `src/capi/capi_internal.hpp` (EDIT) — `SessionLiveness` token; dual-flavour `fixpp_msg` + per-message arena; `OutboundAccumulator`/`AccumulatorEntry`/`GroupInstance`; `fixpp_group_builder`/`fixpp_entry`; `SessionSlot.send_cb`; `CapiApplication::toApp`.
> - `include/fix/c_api/{message,session,error,version}.h` (NEW/EDIT) — 33 new exported symbols; `[1400,1499]` error block; `fixpp_toapp_verdict` enum + `fixpp_send_cb`; MINOR 3→4.
> - `src/wire/offset_table.{hpp,cpp}` (EDIT, cross-layer) — `OffsetTable::resource()` getter (D5) + dict-membership guard in `group()` to make CA-010-read `TYPE_MISMATCH` reachable (D1).

> **Coverage note.** Read path: `message_read_test.cpp` (28 tests; alias-not-copy, error arms, group + nested, SC-003 dual gate via `message_read_mallocnesia`). Write path: `message_write_test.cpp` (round-trip SC-001 peer-receive, framing reject, over-cap, tombstone FR-009a BOTH orderings under ASan+TSan, group build + nested + LIFO; SC-003 via `message_write_mallocnesia`), `msg_clone_cross_strand_test.cpp` (seam #13 SC-006). toApp: `toapp_callback_test.cpp` (6: send/veto/error/out-of-range/framed-view/post-start) + `toapp_alloc_guard` (+mallocnesia). Error block: `error_surface_test.cpp` (oracle re-pointed 5 arms + csv) + `error_block_test.cpp` (translate unit + per-code minor downgrade + live 5-arm SC-004). ABI gates: nm golden (65 symbols), `check_capi_occupancy.sh` (Check A +6, [0,99]/prior-doc-97 unchanged), `check_capi_reentrancy.sh`, abidiff additive (SC-005). Named limitations: L-051-1 (log/otel deferred), L-051-2 (outbound-clone deferred), L-051-3 (empty-group NoXxx=0 edge). Deviations D1–D5 in `.specify/decisions/051-...-deviations.md`. Coverage gate (`[const §IX.1]`, per-file DA/BRDA, `linux-clang-coverage` preset) at `/speckit-verify`.

## 052-c-abi-python-readiness — C ABI Python-readiness (CA-011/012/013, 2026-06-26)

> **New/changed source (the coverage surface):**
> - `src/capi/dictionary.cpp` (NEW) — CA-011 `fixpp_dict_load_from_xml` (construction-time thunk over `dict::XmlLoader::load`; `CAPI_CONFIG_INVALID` on throw; `*out=NULL` every failure path) + `fixpp_dict_destroy` (full-critical-section process-global mutex: tag check → `dict.reset()` → `tag_=DEAD` → allocation-free intrusive dead-shell list (O(load/destroy cycles), L-052-4); NULL-safe; TSan-clean concurrent double-destroy).
> - `src/capi/config.cpp` (EDIT) — CA-012 `fixpp_session_config_set_tcp_endpoint` (empty-host guard; sets `reconnect_endpoint` + `transport_send` placeholder — L-050-5 seam promoted) + `fixpp_session_config_set_reset_seqnum_policy` (FFI-safe memcpy enum guard → out-of-range `CAPI_CONFIG_INVALID`).
> - `src/capi/session.cpp` (EDIT) — CA-012 `fixpp_session_acceptor_bound_endpoint` (THREAD_SAFE port-0 readback over `Engine::acceptor_bound_endpoint`; not-yet-bound → `*port_out=0`+OK; steady-state abort-on-escape thunk).
> - `src/capi/message_read.cpp` (EDIT) — CA-013 `fixpp_msg_field_count`/`fixpp_msg_field_at` over `OffsetTable::entries()` (wire/document-order multiset; positive MSG-tag guard; `INDEX_OUT_OF_RANGE`; aliasing view, zero global heap).
> - `src/capi/capi_internal.hpp` (EDIT) — `fixpp_dict` tombstone (`tag_` first member + ctor; `FIXPP_HANDLE_TAG_DICT=0xD1C70DEF`).
> - `include/fix/c_api/{dict.h (NEW),session.h,message.h,c_api.h,version.h} (EDIT)` — 7 new exported symbols + `fixpp_msg_field_t` PoD + `fixpp_reset_seqnum_policy` enum; umbrella aggregates `dict.h`; MINOR 4→5.

> **Coverage note.** US1 dict: `dictionary_load_test.cpp` (12; bundled FIX42/44/50SP2/T11 load + set_dictionary roundtrip, bad-path/malformed/NULL no-abort, sequential + NULL + TSan concurrent double-destroy SC-004). US2 endpoint: `public_roundtrip_test.cpp` (9; two-engine pure-public-header round-trip SC-001, D-4 production-default bilateral_strict establish 20/20 + 15× TSan stress, port-0 readback, setter NULL/empty/out-of-range arms, E1 dict-destroy-before-open witness). US3 iteration: `message_field_iteration_test.cpp` (11; wire-order mutation-discriminating [sorted impl FAILS], repeating-group 453/448/447 superset SC-002, value-byte-equals-get_string offset cross-check, value-aliases-wire, type-mismatch ENGINE-tag + DEAD + index-OOR, zero-heap SC-003 via `message_field_iteration_mallocnesia`, clone cross-thread). ABI gates: nm golden (72 symbols, +7), `check_capi_occupancy.sh` UNCHANGED (48 codes, NO new error codes FR-010), `check_capi_reentrancy.sh` (+7 annotated), pure-C umbrella exposure (T024). Sanitizer matrix (ASan/UBSan/TSan over the 3 new tests) zero findings. Named limitations: L-052-1 (XML-path only), L-052-2 (primitive host/port only), L-052-3 (inbound/parsed iteration only). Coverage gate (`[const §IX.1]`, per-file DA/BRDA, `linux-clang-coverage` preset) at `/speckit-verify`.
