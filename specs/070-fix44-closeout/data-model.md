# Data Model: 070-fix44-closeout

Entities added/extended by this feature. All additions are **opt-in** — every default
value reproduces pre-feature behavior byte-for-byte (FR-012). No C-ABI shape changes (FR-013).

Legend: **FR** links the field to its functional requirement; **Default** is the value
that yields the byte-identical baseline.

---

## E1 — `session_posture` (new enum) + `SessionConfig::posture` (new field)

**Location.** `include/fixpp/session/session_config.hpp` (enum near the top of the
`fixpp::session` namespace; field inside `SessionConfig`, struct body ends `:474`).

```cpp
enum class session_posture { production, test };
```

| Field | Type | Default | Semantics | FR |
|-------|------|---------|-----------|----|
| `SessionConfig::posture` | `std::optional<session_posture>` | `std::nullopt` | Local test/production posture. `nullopt` ⇒ enforcement **disabled** (no new rejection path fires). `production` ⇒ refuse an inbound Logon marked test (`464=Y`). `test` ⇒ refuse an inbound Logon marked production (`464=N` or absent). Symmetric rule (research.md D-A). | FR-001, FR-002, FR-003 |

**Validation rules.**
- No value is invalid; a two-valued enum wrapped in `optional` is total.
- Interpretation of the inbound peer indication (not stored — computed per Logon):
  `peer_is_test := (464 == "Y")`; `464 == "N"` or absent ⇒ `peer_is_production`.
  Mismatch := `(posture == production && peer_is_test) || (posture == test && !peer_is_test)`.
- A `464` present with a value other than `Y`/`N` ⇒ malformed (Edge Cases / S-029), handled by the existing malformed-header path — **not** by posture logic. (464 is `type='BOOLEAN'` domain `{Y,N}` in `FIX44.xml:5246-5249`.)

---

## E2 — `SessionConfig::advertised_max_message_size` (new field)

**Location.** `include/fixpp/session/session_config.hpp`, inside `SessionConfig`.

| Field | Type | Default | Semantics | FR |
|-------|------|---------|-----------|----|
| `SessionConfig::advertised_max_message_size` | `std::optional<std::uint32_t>` | `std::nullopt` | Bytes. When set: (a) emit `MaxMessageSize(383)=<value>` in our outbound Logon; (b) hard-enforce inbound — disconnect any inbound frame whose `frame.size()` exceeds this value. `nullopt` ⇒ no 383 emitted, no negotiated enforcement (only the absolute framer backstop applies). | FR-004, FR-005 |

**Validation rules.**
- `uint32_t` is the on-wire domain match for `MaxMessageSize` (`type='LENGTH'`, `FIX44.xml:4995`).
- No clamp against the framer backstop (`default_max_frame_bytes` 256 KiB, `framer.hpp:21`): if the configured value exceeds the backstop, the framer still governs the outer envelope (research.md D-C / Edge Cases S-030). The negotiated bound only ever tightens.
- Boundary: exactly `N` accepted; `N+1` disconnects (SC-003).

---

## E3 — `supported_msg_type` (new struct) + `SessionConfig::supported_msg_types` (new field)

**Location.** `include/fixpp/session/session_config.hpp`.

```cpp
struct supported_msg_type {
    char        direction;   // MsgDirection(385) — on-wire CHAR; conformant {'S','R'}
    std::string msg_type;    // RefMsgType(372)   — MsgType string, e.g. "D", "8"
};
```

| Field | Type | Default | Semantics | FR |
|-------|------|---------|-----------|----|
| `SessionConfig::supported_msg_types` | `std::vector<supported_msg_type>` | `{}` (empty) | Ordered advertise list. Non-empty ⇒ emit `NoMsgTypes(384)=k` + k `(385,372)` member pairs in this order in the outbound Logon. Empty ⇒ no 384 group emitted at all (no `384=0`). | FR-008 |

**Validation rules.**
- `direction` is written verbatim (operator-supplied `char`); conformant values are `S`/`R` (`FIX44.xml:4997-5000`). No advertise-side enum guard this feature (research.md D-D) — supply `S`/`R`.
- `msg_type` is written verbatim as the `372` value.
- Order is significant: the emitted group members appear in vector order (SC-004 exact-set + order round-trip).
- **Copy-constructibility.** `supported_msg_type` (a `char` + `std::string`) and `std::vector<supported_msg_type>` are copy-constructible; `std::optional<session_posture>` and `std::optional<std::uint32_t>` are copy-constructible. The `static_assert(std::is_copy_constructible_v<SessionConfig>)` at `session_config.hpp:483-487` **continues to hold** — no field breaks the by-value `Session::cfg_` membership (FR-001 / 010 W-5).

---

## E4 — `logon_advertise_options` (new struct) + `build_logon` signature extension

**Location.** `include/fixpp/session/admin_messages.hpp` (struct above `build_logon`
decl at `:54`); consumed in `src/session/admin_messages.cpp:79`.

```cpp
struct logon_advertise_options {
    std::optional<std::uint32_t>        max_message_size{};              // 383, emit when set
    bool                                test_message_indicator = false;  // 464=Y when true
    std::span<const supported_msg_type> supported_msg_types{};           // 384 group; empty ⇒ omit
};
```

| Member | Type | Default | Emits |
|--------|------|---------|-------|
| `max_message_size` | `std::optional<std::uint32_t>` | `nullopt` | `MaxMessageSize(383)=<value>` when set |
| `test_message_indicator` | `bool` | `false` | `TestMessageIndicator(464)=Y` when `true` (never emits `464=N`) |
| `supported_msg_types` | `std::span<const supported_msg_type>` | `{}` | `NoMsgTypes(384)=k` + k `(385,372)` pairs when non-empty |

**Signature change.** `build_logon` gains **one** trailing parameter:
```cpp
… std::optional<std::string_view> password = std::nullopt,
  const logon_advertise_options& opts = {}) noexcept;   // NEW, trailing, defaulted
```

**Validation / discipline rules.**
- Non-owning: `supported_msg_types` is a `std::span` into the caller's `SessionConfig::supported_msg_types` vector — `build_logon` performs **no allocation** (Article XV.1). `opts` passed by `const&`.
- Default-constructed `opts` (`{}`) ⇒ no 383/464/384 emitted ⇒ byte-identical to the pre-feature Logon (FR-012).
- The 384 group is written via the existing bound-checked `wire::Writer::append_raw`; overflow ⇒ `std::unexpected(wire_*)` (fail-closed, no partial frame) — same pattern as 553/554/789 (`admin_messages.cpp:174-207`).

---

## E5 — `FrameHeader` additions (inbound header pre-scan)

**Location.** `src/session/scan_frame_header.hpp` — `FrameHeader` struct (`:38-58`), tag
switch (`:103-153`).

| New member | Type | Populated from | Purpose | FR |
|------------|------|----------------|---------|----|
| `test_message_indicator` | `std::string_view` (raw value of tag 464) | `case 464:` in the scan switch | Read on inbound Logon for posture enforcement (empty ⇒ absent ⇒ peer production). | FR-002 |
| `max_message_size` | `std::string_view` (raw value of tag 383) | `case 383:` in the scan switch | Read on inbound Logon to capture the peer's advertised 383 into session state (E6). | FR-007 |

**Validation rules.**
- Both are zero-copy views into the caller's `frame` span (consistent with every other `FrameHeader` member; no heap).
- `test_message_indicator` empty ⇒ 464 absent ⇒ treated as production (symmetric rule).
- `max_message_size` is captured as a raw view; the numeric parse to `std::uint32_t` for E6 happens at the read site (session.cpp), matching how `msg_seq_num` is parsed via `parse_seqnum`.
- No new include: `<string_view>` already included (`scan_frame_header.hpp:24`).

---

## E6 — Session state addition: captured peer MaxMessageSize

**Location.** `include/fixpp/session/session.hpp` — a new private `Session` member;
set on the inbound Logon path in `src/session/session.cpp`.

| Field | Type | Default | Semantics | FR |
|-------|------|---------|-----------|----|
| `Session::peer_max_message_size_` (name illustrative) | `std::optional<std::uint32_t>` | `std::nullopt` | The peer's advertised `MaxMessageSize(383)` captured from its Logon (E5), for observability. **Not** used for a hard outbound guard this feature (Clarification Q2 / research.md D-A). `nullopt` ⇒ peer advertised no 383. | FR-007 |

**Validation rules.**
- Written once when an inbound Logon carrying 383 is interpreted; read-only thereafter (informational).
- No behavior is gated on this field this feature — it is captured observable state only (spec Assumptions / S-030 enforcement direction).

---

## Entity relationships & invariants

- **INV-070-1 (default no-op).** `posture == nullopt` ∧ `advertised_max_message_size == nullopt` ∧ `supported_msg_types.empty()` ⇒ every new code path is inert and the engine is byte/disposition-identical to baseline (FR-012, SC-006).
- **INV-070-2 (advertise ⇒ config-driven `opts`).** The outbound Logon's 383/464/384 fields are a pure function of `SessionConfig` (`opts` derived at the two `build_logon` call sites, `session.cpp:838` initiator / `:2490` acceptor); `test_message_indicator` in `opts` is `(cfg_.posture == session_posture::test)`.
- **INV-070-3 (inbound-only 383 enforcement).** Only `advertised_max_message_size` (ours) gates the inbound disconnect; `peer_max_message_size_` (E6) never triggers a disconnect this feature.
- **INV-070-4 (layered size limits).** Framer backstop (`framer.hpp:21`, unchanged) ⊇ negotiated bound; the negotiated check only tightens (research.md D-C).
- **INV-070-5 (copy-constructible config).** All new `SessionConfig` fields keep `static_assert(is_copy_constructible_v<SessionConfig>)` (`:483`) true.
