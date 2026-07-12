# Research: 070-fix44-closeout

Phase 0 decisions. Each records Decision / Rationale / Alternatives, grounded in
source anchors (verified `file:line`) and the spec Clarifications.

Anchors verified while writing (differences from the /plan brief noted where they occur):
- `include/fixpp/session/session_config.hpp` — struct body ends `};` at **:474**; copy-constructible `static_assert` at **:483-487** (brief said "~L474 + static_assert just after" — confirmed).
- `include/fixpp/session/admin_messages.hpp:54` — `build_logon` decl (confirmed). Impl `src/session/admin_messages.cpp:79` (confirmed).
- `src/session/scan_frame_header.hpp:38` — `FrameHeader` struct; tag switch starts **:103** (confirmed).
- `src/session/session.cpp:1961` — `on_inbound_frame` (confirmed). Validator gate **:1976**; `validate_inbound_` helper **:1869**. `interpret_logon` call **:1992**, refusal path **:1998-2007**. Acceptor reply `build_logon` **:2490**. Initiator emit `build_logon` **:838**. Logon-time Logout+disconnect disposition **:2676-2702**. `fromApp`/`is_admin_msgtype` dispatch **:3478-3504**.
- `include/fixpp/wire/framer.hpp:21` — `default_max_frame_bytes = 256 KiB` (confirmed).
- `dictionaries/FIX44.xml` — 385 def **:4997-5000**, 464 def **:5246-5249**, 383 **:4995**, 384 **:4996**, 372 **:4936**, 212/213 **:4479-4480**; XMLnonFIX message def **:1008**; `<header>` block **:2-34** (212/213 as header fields at header lines 24-25).

---

## D-A — The four /clarify resolutions (encode, do not re-litigate)

**Decision.** Adopt the four spec Clarifications verbatim as the design contract:

1. **S-029 posture rule = symmetric, absent ⇒ production.** `464=Y` ⇒ peer is test; `464=N` **or absent** ⇒ peer is production. A `production`-posture session refuses only `464=Y`; a `test`-posture session refuses absent/`464=N`. Blocks both cross-connect directions.
2. **S-030 = inbound-only hard enforce.** Disconnect a peer exceeding the size *we* advertised. The peer's advertised 383 is captured for observability (FR-007) but there is **no** hard outbound guard this feature.
3. **S-037 = explicit operator config list.** An ordered list of `(MsgDirection, MsgType)` pairs; empty default ⇒ no 384 group. No auto-derivation from the dictionary.
4. **A-034 validator = accept in all configs.** A well-formed 35=n is accepted regardless of `validate_inbound_messages`.

**Rationale.** Each was resolved against reference-engine behavior recorded in the spec: QuickFIX-cpp implements neither 383 enforcement nor advertisement (field defined, zero Session logic), does not advertise NoMsgTypes at all, and there is no interop pressure to build an outbound 383 guard now. The symmetric-464 rule is the only interpretation that blocks both cross-connect directions (a production book must never accept test-marked traffic, and vice-versa).

**Alternatives rejected.**
- Absent-464 ⇒ "unknown / accept" (asymmetric): rejected — a test peer that simply omits 464 would slip into a production session, defeating the safety property (US1).
- S-030 bidirectional enforcement (also block our own oversized outbound against the peer's 383): rejected this feature — no interop pressure (QuickFIX does neither), and it adds an outbound guard with no red-provable counterparty. Captured as observable state (FR-007) so a later feature can add it without a config change.
- S-037 auto-derivation from the loaded dictionary: rejected — non-deterministic advertised set, couples the wire to codegen scope; operator intent is the correct source (Clarification Q3).

---

## D-B — `logon_advertise_options` struct vs. more positional optionals

**Decision.** Add **one** trailing parameter `const logon_advertise_options& opts = {}` to `build_logon`, bundling the three advertisements:

```cpp
struct logon_advertise_options {
    std::optional<std::uint32_t> max_message_size;        // 383, emitted when set
    bool                          test_message_indicator = false;  // 464=Y when true
    std::span<const supported_msg_type> supported_msg_types{};     // 384 group, empty ⇒ omitted
};
```

`build_logon` today has 11 params, 6 trailing optionals (`admin_messages.hpp:54-61`). Three of the four capabilities advertise on the outbound Logon, so without a bundle the signature would grow to 14 params.

**Rationale.**
- **Readability + call-site safety.** `build_logon` already carries a `NOLINT(bugprone-easily-swappable-parameters)` suppression (`admin_messages.cpp:77`) because of adjacent same-typed args; adding three more positional optionals worsens exactly the swap hazard the suppression flags. A named struct makes each advertisement explicit at the call site.
- **Zero-alloc preserved.** `opts` is passed by `const&` with a `= {}` default; `build_logon` stays `noexcept` and reads the members without copying. The `supported_msg_types` is a `std::span` (non-owning view into the caller's `SessionConfig` vector) — no allocation inside the builder (Article XV.1). Default-constructed `opts` (empty span, `nullopt`, `false`) ⇒ byte-identical output ⇒ FR-012.
- **Two call sites only.** Initiator emit (`session.cpp:838`) and acceptor reply (`session.cpp:2490`); both construct `opts` from `cfg_`. All other `build_logon` callers (tests) keep the default and are byte-identical.

**Alternatives rejected.** Three more positional `std::optional`/`bool`/list params — rejected per the swap-hazard + 14-arg readability argument above. A separate `build_logon_v2` overload — rejected as gratuitous surface duplication for a single additive struct.

---

## D-C — S-030 negotiated-size enforcement vs. the framer backstop

**Decision.** Two **independent, layered** size limits:
- **Outer absolute backstop (UNCHANGED):** `wire::Framer` rejects any frame larger than `default_max_frame_bytes` (256 KiB, `framer.hpp:21`) / the configured `max_frame_bytes` at the framing stage, *before* a frame ever reaches `on_inbound_frame`. This governs unconditionally, always.
- **Inner negotiated bound (NEW, opt-in):** at the **top of `on_inbound_frame`** (`session.cpp:1961`), when `cfg_.advertised_max_message_size` is set, if `frame.size()` exceeds that value, disconnect with a distinct "negotiated max message size exceeded" reason. Boundary: exactly `N` accepted, `N+1` disconnects (SC-003).

**Rationale.** The framer runs first and caps the outer envelope; the negotiated check is a stricter-or-equal per-session inner bound applied to an already-framed message. Placing the check at the top of `on_inbound_frame` (before FSM/seqnum work) means it fires for every inbound frame in every state, and it reads `frame.size()` — the total on-wire length — matching FR-005's "total on-wire length". FR-006 is structurally satisfied: the negotiated check is *additional* code on a separate `std::optional` gate; it never touches `framer.cpp` and cannot weaken the backstop.

**Edge case (spec Edge Cases / S-030).** If a configured `N` exceeds the framer backstop, the backstop still governs (a frame > 256 KiB never reaches `on_inbound_frame`, so the peer is disconnected by the framer first). The negotiated check only ever *tightens*. No clamp of `N` to the backstop is applied — the two limits compose by min-semantics naturally.

**Alternatives rejected.**
- Enforce inside the framer by lowering `max_frame_bytes` to `N`: rejected — conflates two contracts (framer error `wire_frame_too_large` vs. session-level negotiated disconnect), loses the distinct reason (FR-005), and would make the negotiated bound governed by framer error handling rather than a session disconnect.
- Enforce after parse/dispatch: rejected — wastes work parsing an over-size message and risks the message reaching a callback before the disconnect (FR-005 requires refusing the over-size message, not delivering-then-disconnecting).

---

## D-D — MsgDirection(385) on-wire value (FIX44.xml finding)

**Decision.** The advertised `385` value is an **operator-supplied `char`** carried verbatim from `supported_msg_type::direction`; fixpp does **not** hard-code or transcode it. The **FIX 4.4 conformant enumerators** are `S` (SEND) and `R` (RECEIVE).

**Finding (verified).** `dictionaries/FIX44.xml:4997-5000`:
```xml
<field number='385' name='MsgDirection' type='CHAR'>
 <value enum='S' description='SEND' />
 <value enum='R' description='RECEIVE' />
</field>
```
So the on-wire domain per the FIX44 dictionary is `{S, R}` — **not** `{0, 1}`. The brief's caution is confirmed: a QuickFIX test used `'0'`/`'1'`, but QuickFIX's own `FixValues` defines `MsgDirection_SEND = 'S'` / `MsgDirection_RECEIVE = 'R'`, matching the dictionary. Hard-coding `'0'`/`'1'` would emit a value outside the FIX44 enum.

**Design risk surfaced.** Because `direction` is a raw operator `char`, an operator could advertise a non-conformant value (e.g. `'0'`). This feature does **not** validate `direction` against `{S,R}` on the advertise side — the field is written as-is (documenting the operator-supplied contract, matching how the value flows). Rationale: the outbound Logon body is not run through the inbound dictionary validator; adding an advertise-side enum guard is scope-creep and would be a *new* failure disposition not requested by any FR. Documented in contracts + quickstart as "supply `S`/`R`". Inbound parse of a peer's 384 group is already tolerated (`test_066_arena_fit_test.cpp`) and out of scope for validation here.

**Alternatives rejected.** A typed `enum class msg_direction { send /*'S'*/, receive /*'R'*/ }` in the config: reasonable, but the spec Key Entities model the pair as `(MsgDirection, MsgType)` with `MsgType` already a free `std::string`; keeping `direction` a `char` matches the on-wire CHAR type and avoids an enum→char render step in the zero-alloc builder. Recorded as a candidate hardening (L-070 follow-up) but not adopted, to keep the builder change minimal.

---

## D-E — A-034 validator-accepts finding (DECISIVE — no code allowance needed)

**Decision.** FR-011 is satisfied by the **existing** validator with **no code change**. A well-formed inbound XMLnonFIX (35=n) is already accepted by `wire::dictionary_driven_validator::validate` even with `validate_inbound_messages` enabled. The A-034 deliverable is therefore a **discriminating test only** (no allowance in `validator.hpp` / `validate_inbound_`).

**Finding (verified, source-grounded).**
1. **XMLnonFIX has an empty message body.** `dictionaries/FIX44.xml:1008`: `<message name='XMLnonFIX' msgtype='n' msgcat='admin' />` — self-closing, no field children.
2. **212/213 are HEADER fields.** The `<header>` block (`FIX44.xml:2-34`) declares `XmlDataLen`(212) and `XmlData`(213) at header lines 24-25 (`required='N'`).
3. **Every message's valid-tag set includes the header fields.** The loader builds each message's field list as **header fields first, then message-specific, then trailer** (`src/dictionary/xml_loader.cpp:738-745` — `expand_field_list(header_node_, …)` runs before `expand_field_list(md.node, …)`). `Dictionary::as_table_view()` then populates `valid_[msg_type]` from that full expansion (`src/dictionary/dictionary.cpp:314-320`).
4. **Therefore `field_valid_for("n", 212)` and `field_valid_for("n", 213)` return `true`** (`table_view.hpp:197-201`; `dictionary.cpp:222-223`). The validator's Step 1 unexpected-tag check (`validator.hpp:143-145`) does **not** reject 212/213 for msg `n`.
5. **Step 2 required-fields scan passes.** XMLnonFIX declares no body-required fields; the header-required tags (8/9/34/35/49/52/56) are present in any well-formed message, and framing tags 8/9/10 are treated as satisfied (`validator.hpp:160-175`).
6. **Steps 0 and 3 pass.** First non-framing field is 35 (header-order OK, `validator.hpp:118-135`); XMLnonFIX has no repeating group (`validator.hpp:203-278` no-op). 212 is `LENGTH`, 213 is `DATA` → `check_field_type` default case, no structural constraint (`validator.hpp:380-387`).

**Corollary — SOH-safety.** The parser's LEN+DATA handling reads 213's value for exactly `212=len` bytes (offset-table entry spans the full XML including embedded SOH), so the validator sees tag 213 **once** with the whole payload; embedded SOH is not mis-parsed as new fields. (Inbound parse already proven by `tests/session/test_066_arena_fit_test.cpp`.)

**Design risk surfaced.** This finding is *load-bearing and non-obvious*: it depends on 212/213 being HEADER fields in the specific loaded dictionary. A dictionary variant that moved XmlDataLen/XmlData out of `<header>` into a message-body-only definition would flip `field_valid_for("n", 212/213)` to `false` and the validator would reject 35=n with `wire_unexpected_tag`. The discriminating test MUST run against the shipped `FIX44.xml` (header-resident 212/213) with `validate_inbound_messages = true`, asserting **no reject** — this pins the finding and turns a future dictionary regression into a red test rather than a silent rejection.

**Alternatives rejected.** Add an explicit "XMLnonFIX bypass" branch in `validate_inbound_` (`session.cpp:1869`) or a `field_valid_for` special-case for msg `n`: rejected — the existing dictionary-driven path already yields the correct answer; a bypass would be dead code (Karpathy §2 — no code for a scenario that already works) and would hide the header-field dependency instead of pinning it with a test.

---

## D-F — Reject/disconnect disposition for S-029 and S-030 (which existing disposition to mirror)

**Decision.**
- **S-029 posture mismatch → emit Logout(35=5) with distinct posture-mismatch text, then Disconnect.** Mirror the existing **Logon-time Logout+disconnect** disposition at `session.cpp:2676-2702`: `build_logout(... text ...)` → `fire_to_admin_` → `assign_outbound` → `store_then_emit` → `record_state_transition_(fsm_state::Disconnected)` → `co_return` empty. The posture check is placed **after `interpret_logon` succeeds and after the header scan, before the acceptor reply build** (`session.cpp:2490`), so a mismatch never reaches Active/Established (FR-002).
- **S-030 negotiated-size exceeded → Disconnect with a distinct reason.** Because the check fires at the **top of `on_inbound_frame`** before any frame is interpreted (and applies to app + admin frames alike, in any state), the disposition is a straight `record_state_transition_(fsm_state::Disconnected)` with the distinct "negotiated max message size exceeded" reason — mirroring the session-fatal Disconnected transitions already used across the inbound path (e.g. `session.cpp:2498-2499`, `2504-2505`). No Logout is emitted for S-030 (the peer violated a size contract mid-stream; the framer/transport-level disposition is a disconnect, consistent with `wire_frame_too_large` handling).

**Rationale (and reconciliation of the brief wording).** The /plan brief says S-029 mirrors "the existing inbound-Logon validation-failure disposition (interpret_logon failure path)". The literal `interpret_logon` refusal path (`session.cpp:1998-2007`) is a **silent** `record_state_transition_(Disconnected)` with **no** wire notification. FR-002 explicitly requires "reject/logout + disconnect … surfacing a distinct posture-mismatch reason" — a *wire* notification. So the brief's "(interpret_logon failure path)" is read as pointing to the **refuse-and-disconnect *outcome*** (does not reach Active), **not** its silent mechanism. The correct mechanism to reuse is the Logon-time Logout+disconnect at `session.cpp:2676-2702`, which already: builds a Logout with text, observes it via `toAdmin`, transmits it, and transitions to Disconnected. This is deliberate and recorded so a reviewer sees the choice was not a silent reconciliation. (Precedent for the fail-closed-with-notification-then-disconnect shape: [[feedback_mirror_existing_failclosed_disposition]].)

**Alternatives rejected.**
- S-029 silent disconnect (reuse `interpret_logon` path literally): rejected — violates FR-002's "surfacing a distinct posture-mismatch reason"; a silent drop gives the mis-configured operator no signal.
- S-029 session-level Reject(35=3) instead of Logout(35=5): rejected — a Reject implies a recoverable field-level error and keeps the session negotiating; posture mismatch is terminal (the session must not be established), so Logout(35=5) + disconnect is the correct FIX shape, matching the Logon-time Logout precedent.
- S-030 Logout before disconnect: rejected — the size check runs before frame interpretation and outside the Logon handshake; emitting a well-formed Logout after already deciding the peer breached the size contract adds a build/emit path for no protocol benefit and is inconsistent with the framer's disconnect-on-oversize backstop shape.
