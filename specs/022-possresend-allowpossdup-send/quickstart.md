# Quickstart: Exercising PossResend(97) Inbound + AllowPosDup Send Strip

How to drive and verify this slice. All paths are repo-relative to the library submodule.

## US2 — AllowPosDup send-path strip (the production change)

Unit witnesses (`tests/session/test_send_allow_pos_dup_strip.cpp`, RED-first):

1. **Strip default**: build a session with default `SessionConfig` (`allow_pos_dup == false`); `send` a payload `35=D\x01...43=Y\x01...122=20260605-...\x01...`; capture the framed bytes; assert the application portion contains **no** boundary `43=` and **no** boundary `122=`.
2. **Retain**: set `allow_pos_dup = true`; same payload; assert both `43=Y` and `122=...` survive verbatim.
3. **Embedded-text `43=`-in-a-value hostile witness** ([[feedback_delimiter_injection_verbatim_field_copy]]): payload with a field whose *value* contains the literal bytes `43=` with NO preceding SOH (e.g. `11=ORD43=Y\x01`) plus a separate real boundary `43=Y\x01`; default-strip; assert the real boundary field is removed but `11=ORD43=Y` is left **intact** (the embedded `43=` is not a boundary match). RED-prove: an unanchored strip would corrupt `11=`.
4. **True SOH-boundary excision**: payload with a genuine SOH-boundary `…\x0143=Y\x01` and `…\x01122=…\x01`; default-strip → assert both boundary fields are removed; flip `allow_pos_dup = true` → assert both are retained verbatim (the boundary `43=`/`122=` is excised under default and retained only by explicit operator opt-in).
5. **Malformed-field fail-closed** (`Strip_MalformedField_Returns131_NoSeqnum_NoTransmit_NoExcision`, parameterized over cases that pass today's 020 floor): `35=D\x0111BROKEN\x0143=Y\x01` (a field with no `=`), `35=D\x01=bad\x01122=x\x01` (an empty-tag field), `35=D\x014a=x\x0143=Y\x01` (a non-digit-tag field), and `35=D\x01\x0143=Y\x01` (a stray `\x01` yielding a zero-length field); default-strip; assert `app_payload_malformed=131`, the outbound seqnum is NOT consumed, nothing is transmitted, and NO excision occurred. RED-prove: this test is only meaningful once the 022 scanner exists — today's 020 denylist accepts all four (each is SOH-terminated, leads `35=`, has a non-empty MsgType, no dup `35=`, no banned tag). (Do NOT use a "no final SOH" case here — the 020 floor already rejects that at `session.cpp:2951`, so it would measure the floor, not the scanner.)
6. **No-op when absent**: payload with no `43`/`122`; assert byte-identical output under both knob settings.
7. **Resend independence** (FR-007): set `allow_pos_dup=false`, `send` a payload that **contained** `43`/`122` (so the strip provably removed them from the stored frame), THEN drive a ResendRequest reply / replay; assert the replayed frame (`build_replay_frame`) still carries `43=Y`+`122`. (A send with no 43/122 passes trivially and would not detect a knob-leak into the resend path.)
8. **No-heap**: a dedicated strip no-heap cell inside `test_send_allow_pos_dup_strip.cpp` passes under the mallocnesia LD_PRELOAD gate (`/speckit-verify` Step 6). `test_session_alloc_guard` covers only the 005-era seqnum + admin-build paths and does NOT exercise `send_impl`; the binding no-heap gate for the strip is the new cell in `test_send_allow_pos_dup_strip.cpp`.

## US1 — Inbound PossResend(97) (witness-only)

Unit/parity witnesses (`tests/session/test_inbound_poss_resend.cpp`, RED-first against the disposition table):

1. **Deliver in-sequence `97=Y`**: feed an established session an in-sequence app message with `97=Y`; assert expected inbound seqnum N→N+1, `Application::fromApp` is invoked with the full frame (tag 97 readable), and no `Reject`/`Logout`/disconnect.
2. **No-app byte-identity**: with no `Application` registered, assert the `97=Y` message is handled identically to the same message without `97`.
3. **`43=Y`+`97=Y`**: assert the 021 PossDup arms fire on `43` only and `97` adds no reject.
4. **`97=Y` without `122`**: assert no `Reject(371=122)` (122-required keys on `43=Y` only).

## Live interop cells (SC-005, both roles)

Extend the 018/020 fixture + parent `phase-9-harness/`:

- **AllowPosDup wire-capture cell**: fixpp (default knob) sends an app message; capture outbound bytes via the engine-log-seam (016 P4); assert no 43/122 in the application portion; QFcpp/QFJ accept it.
- **PossResend deliver cell**: the QF counterparty app sends a `97=Y` business message; assert fixpp delivers it to `fromApp` and the session stays established.
- Run under normal + ASan/UBSan/TSan (018/020 discipline); skip-without-counterparty.

## Build / verify

```bash
cd research/G19-fix-fpml-iso20022/library
# debug build + the new unit suites
ctest --preset <debug> -R "send_allow_pos_dup_strip|inbound_poss_resend"
# full Tier-1 mirror (sanitizers, coverage, mallocnesia, interop) at /speckit-verify:
#   UNFILTERED Tier-1 (or -L sync) per the §XV.9 awaitable-include watch-item
```
