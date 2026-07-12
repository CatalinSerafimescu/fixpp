# Contract: SessionConfig extensions + Logon emission + inbound dispositions

Behavioral contract for 070-fix44-closeout. Each clause maps to its FR and cites the
source anchor it constrains. **Invariant (C-0 / FR-012):** with `posture == nullopt`,
`advertised_max_message_size == nullopt`, and `supported_msg_types` empty, every clause
below is inert and the engine is byte-for-byte and disposition-for-disposition identical
to the pre-feature baseline.

---

## C-1 — New `SessionConfig` fields (surface)

`include/fixpp/session/session_config.hpp` (struct body ends `:474`; copy-constructible
`static_assert` at `:483`).

| Name | Type | Default | Contract | FR |
|------|------|---------|----------|----|
| `posture` | `std::optional<session_posture>` (`enum class session_posture { production, test }`) | `nullopt` | Enables S-029 enforcement; drives inbound refusal + outbound `464=Y` advertise. | FR-001 |
| `advertised_max_message_size` | `std::optional<std::uint32_t>` | `nullopt` | Emits `383` outbound; hard-enforces inbound size. | FR-004, FR-005 |
| `supported_msg_types` | `std::vector<supported_msg_type>` (`{char direction; std::string msg_type;}`) | `{}` | Emits the `384` group outbound in order. | FR-008 |

**C-1.1** All three additions keep `SessionConfig` copy-constructible (`static_assert` at
`:483-487` must still compile). No C-ABI exposure (FR-013) — C++ config surface only.

---

## C-2 — `build_logon` signature change

`include/fixpp/session/admin_messages.hpp:54` (decl) / `src/session/admin_messages.cpp:79` (impl).

**C-2.1** One new trailing parameter, defaulted:
```cpp
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_logon(
    std::span<std::byte> out, seqnum_t seq, std::string_view sender_comp_id,
    std::string_view target_comp_id, std::string_view begin_string, int heartbt_int,
    std::string_view sending_time, bool reset_seqnum = false,
    std::optional<seqnum_t> next_expected_seq = std::nullopt,
    std::optional<fixpp::dict::application_version> default_appl_ver_id = std::nullopt,
    std::optional<std::string_view> username = std::nullopt,
    std::optional<std::string_view> password = std::nullopt,
    const logon_advertise_options& opts = {}) noexcept;   // NEW
```
where `logon_advertise_options { std::optional<std::uint32_t> max_message_size; bool test_message_indicator = false; std::span<const supported_msg_type> supported_msg_types; }`.

**C-2.2 (noexcept + zero-alloc preserved).** `build_logon` stays `noexcept`; `opts` is read
by `const&`, `supported_msg_types` is a non-owning span → no heap allocation inside the
builder (Article XV.1). A default-constructed `opts` produces the byte-identical baseline Logon.

**C-2.3 (fail-closed).** Any member append (383/464/384 group) that overflows the caller's
`out` buffer returns `std::unexpected(wire_*)` (e.g. `wire_frame_too_large` /
`wire_field_value_truncated`) with no partial frame — same disposition as the existing
553/554/789 appends (`admin_messages.cpp:174-207`).

**C-2.4 (call sites).** Exactly two production call sites construct `opts` from `cfg_`:
initiator emit `session.cpp:838`, acceptor reply `session.cpp:2490`. All test callers use
the default `opts = {}` and remain byte-identical.

---

## C-3 — Outbound Logon field-emission contract

The new fields are appended within the existing Logon body ordering (`admin_messages.cpp:79-207`),
each emitted **only when its `opts` member is active**:

| Tag | Emitted when | Value | FR |
|-----|--------------|-------|----|
| `MaxMessageSize(383)` | `opts.max_message_size.has_value()` | decimal `<value>` | FR-004 |
| `TestMessageIndicator(464)` | `opts.test_message_indicator == true` (i.e. `cfg_.posture == test`) | `Y` (never `N`) | FR-002 (advertise side) |
| `NoMsgTypes(384)=k` then k × (`MsgDirection(385)=<dir>` `RefMsgType(372)=<type>`) | `!opts.supported_msg_types.empty()` (`k = size`) | count + contiguous member pairs in config order | FR-008 |

**C-3.1 (omission = baseline).** When a member is inactive, its tag(s) are **absent** — no
`383`, no `464`, no `384` (not even `384=0`). This is the FR-012 default path.

**C-3.2 (384 group shape / SC-004).** For a k-entry list, the emitted group is
`384=k` immediately followed by exactly k `(385,372)` pairs, contiguous, in vector order.
Parsing the emitted Logon back yields exactly k members equal to the configured pairs, in
order (exact-set + order round-trip, not subset).

**C-3.3 (385 value / research.md D-D).** `385` is written verbatim from `direction` (operator
`char`); the FIX44-conformant domain is `{'S','R'}` (`FIX44.xml:4997-5000`). No advertise-side
enum validation is performed this feature.

---

## C-4 — Inbound S-029 disposition (posture mismatch)

**C-4.1 (placement).** After `interpret_logon` succeeds (`session.cpp:1992`) and after the
header scan (`scan_frame_header`, which now reads `464`), and **before** the acceptor reply
Logon is built (`session.cpp:2490`) — the session must not reach Active/Established (FR-002).

**C-4.2 (rule).** When `cfg_.posture.has_value()`, compute `peer_is_test := (hdr.test_message_indicator == "Y")`;
`"N"` or empty ⇒ peer production. Mismatch := `(posture==production && peer_is_test) || (posture==test && !peer_is_test)` (symmetric, research.md D-A).

**C-4.3 (disposition on mismatch).** Mirror the Logon-time Logout+disconnect at
`session.cpp:2676-2702`:
`build_logout(out, seq, sender, target, /*text=*/"<distinct posture-mismatch text>", begin_string, sending_time)`
→ `fire_to_admin_(*lo)` → `assign_outbound()` → `store_then_emit(seq, *lo)` →
`record_state_transition_(fsm_state::Disconnected)` → `co_return` empty. The distinct text
surfaces the posture-mismatch reason (FR-002). **Not** the silent `interpret_logon` refusal
path (`session.cpp:1998-2007`), which emits no wire notification (research.md D-F).

**C-4.4 (match ⇒ unchanged).** When posture matches (or `464` maps to the same posture), the
Logon proceeds exactly as today (FR-003, SC-002 zero false rejections).

**C-4.5 (malformed 464).** A `464` value not in `{Y,N}` is malformed and handled by the
existing malformed-header disposition, not by posture logic (Edge Cases / S-029).

---

## C-5 — Inbound S-030 disposition (negotiated size exceeded)

**C-5.1 (placement).** At the **top of `on_inbound_frame`** (`session.cpp:1961`), before FSM
/ seqnum / interpret work, applied to every inbound frame in every state.

**C-5.2 (rule).** When `cfg_.advertised_max_message_size.has_value()` and
`frame.size() > *cfg_.advertised_max_message_size`: `record_state_transition_(fsm_state::Disconnected)`
with the distinct "negotiated max message size exceeded" reason; refuse the frame (do not
interpret/dispatch it). Boundary: `frame.size() == N` accepted, `N+1` disconnects (FR-005, SC-003).

**C-5.3 (independence from backstop / FR-006).** This check is additional to and never weakens
the absolute framer backstop (`framer.hpp:21` `default_max_frame_bytes`, `framer.cpp` — both
UNCHANGED). A frame larger than the backstop is already rejected at framing, before
`on_inbound_frame`. If configured `N` exceeds the backstop, the backstop governs (research.md D-C).

**C-5.4 (peer 383 capture / FR-007).** On an inbound Logon carrying `383`, capture the value
(`hdr.max_message_size`) into `Session::peer_max_message_size_` (observability only). No hard
outbound guard is applied this feature (Clarification Q2).

---

## C-6 — A-034 XMLnonFIX(35=n) delivery + validator-accept contract

**C-6.1 (fromApp delivery / FR-009, FR-010).** An inbound 35=n is delivered to
`Application::fromApp` (not `fromAdmin`, not rejected) with all fields byte-exact, including a
length-delimited `XmlData(213)` that contains embedded SOH (0x01) / `'='`. **No code change**:
`"n"` is not in `is_admin_msgtype` (`msgtype_classifier.hpp:43-50`), so the Active-state
dispatch (`session.cpp:3478-3504`) routes it to `fromApp`; the parser handles the 212/213
LEN+DATA pair SOH-safe (proven by `tests/session/test_066_arena_fit_test.cpp`).

**C-6.2 (validator accepts / FR-011).** With `validate_inbound_messages = true`, a well-formed
35=n is **accepted** (not rejected) by `wire::dictionary_driven_validator::validate`. **No code
change** — the finding (research.md D-E, verified): 212/213 are HEADER fields
(`FIX44.xml` header lines 24-25) prepended into every message's valid-tag set
(`xml_loader.cpp:738-745` → `dictionary.cpp:314-320`), so `field_valid_for("n", 212/213)` is
`true` (`validator.hpp:143-145`); XMLnonFIX declares no body-required fields, so the
required-fields scan passes (`validator.hpp:160-175`); 212/213 are LENGTH/DATA with no
structural constraint (`validator.hpp:380-387`).

**C-6.3 (discriminating witness / FR-014, SC-005).** The A-034 test MUST:
1. Feed an inbound 35=n with `212=len` / `213=<xml containing embedded SOH>`.
2. Assert it is delivered on `fromApp` (NOT `fromAdmin`, NOT rejected) and that reading tag
   213 returns the exact original bytes including embedded SOH.
3. Run the **validation-enabled** path (`validate_inbound_messages = true`) against the shipped
   `FIX44.xml` and assert **no reject** — this pins the header-field dependency (research.md
   D-E risk: a dictionary that moved 212/213 out of `<header>` would flip this to a rejection).

**C-6.3.1 (well-formed frame construction).** The witness frame MUST carry all header-required
fields (8/9/35/34/49/52/56/10). The validator's required-fields scan reads `required_fields("n")`,
which resolves to the required *header* set (`validator.hpp:166-175`); a frame missing e.g. 52
would red with `wire_required_field_missing` — a false-negative unrelated to the 212/213
acceptance being pinned. Build the frame with the standard header fully populated.

**C-6.4 (OPTIONAL, may be dropped).** A typed `build_XMLnonFIX` convenience builder is optional
scope; it delivers no FR and may be omitted without affecting SC-001..SC-006.

---

## FR ↔ contract clause map

| FR | Clause(s) |
|----|-----------|
| FR-001 | C-1 (`posture`) |
| FR-002 | C-3 (464 advertise), C-4 (mismatch refusal + distinct reason) |
| FR-003 | C-4.4 (match ⇒ unchanged) |
| FR-004 | C-1, C-3 (383 emit) |
| FR-005 | C-5.2 (over-size disconnect + distinct reason, N/N+1) |
| FR-006 | C-5.3 (independent of / never weakens the framer backstop) |
| FR-007 | C-5.4 (peer 383 capture) |
| FR-008 | C-1, C-3.2 (384 group emit in order) |
| FR-009 | C-6.1 (fromApp byte-exact) |
| FR-010 | C-6.1 (not rejected on default path) |
| FR-011 | C-6.2 (validator accepts, all configs) |
| FR-012 | C-0 (default no-op), C-2.2, C-3.1 |
| FR-013 | C-1.1 (no C-ABI change) |
| FR-014 | C-6.3 + one discriminating test per capability (plan.md test list) |
