# Quickstart: FIX 4.4 closeout session-negotiation fields (070)

Integrator-facing walkthrough for the four opt-in capabilities. All are configured on
`fixpp::session::SessionConfig`; **doing nothing keeps today's behavior exactly** (no field
appears on the wire, no new refusal fires).

---

## 1. Refuse a test/production cross-connect (S-029 — TestMessageIndicator 464)

Set the local posture. Leave it unset to disable enforcement.

```cpp
fixpp::session::SessionConfig cfg;
// ... existing fields ...
cfg.posture = fixpp::session::session_posture::production;   // or ::test
```

**On the wire (outbound Logon):** if `posture == test`, our Logon carries `464=Y`
(so a well-behaved peer can enforce symmetrically). A `production` posture emits **no** 464.

**What triggers a refusal (inbound Logon):**
- `posture == production` and the peer's Logon has `464=Y` → **refused**.
- `posture == test` and the peer's Logon has `464=N` **or omits 464** → **refused**.

A refusal emits a `Logout(35=5)` with a distinct posture-mismatch text and disconnects; the
session never reaches Established. A matching (or unset) posture proceeds normally.

Rule of thumb: **`464=Y` means "test"; `464=N` or absent means "production".**

---

## 2. Advertise and enforce a maximum message size (S-030 — MaxMessageSize 383)

```cpp
cfg.advertised_max_message_size = 65536;   // bytes; std::optional<std::uint32_t>
```

**On the wire (outbound Logon):** carries `383=65536`.

**What triggers a disconnect (inbound):** once the session is **established**, any inbound frame
whose total on-wire length exceeds the value **you advertised** disconnects the session with a
distinct "negotiated max message size exceeded" reason. Exactly-`N` bytes is accepted; `N+1`
disconnects. (Enforcement is gated on the established state, so a peer's initial Logon is never
disconnected by this check before negotiation completes; oversize handshake frames are still
caught by the absolute backstop.)

**Relationship to the absolute cap:** this negotiated limit is a stricter *inner* bound. The
engine's absolute frame backstop (`default_max_frame_bytes`, 256 KiB) is unchanged and always
applies as the *outer* limit — if you advertise a value larger than the backstop, the backstop
still governs. Leaving `advertised_max_message_size` unset emits no 383 and adds no negotiated
enforcement (only the absolute backstop remains).

The peer's own advertised `383` (from its Logon) is captured for observability but does **not**
create an outbound guard in this release.

---

## 3. Advertise supported message types (S-037 — NoMsgTypes 384)

Provide an explicit, ordered list of `(direction, MsgType)` pairs. Empty ⇒ nothing emitted.

```cpp
using fixpp::session::msg_direction;
cfg.supported_msg_types = {
    { msg_direction::send,    "D" },   // we SEND NewOrderSingle(D)
    { msg_direction::receive, "8" },   // we RECEIVE ExecutionReport(8)
    { msg_direction::receive, "9" },   // we RECEIVE OrderCancelReject(9)
};
```

`direction` is a typed `msg_direction` enum — **`send`** renders to `MsgDirection(385)=S`,
**`receive`** to `385=R` (the FIX 4.4 conformant values; an off-enum direction is
unrepresentable). `msg_type` is `RefMsgType(372)`.

**On the wire (outbound Logon):** carries `NoMsgTypes(384)=3` followed by three contiguous
member pairs `372=D 385=S`, `372=8 385=R`, `372=9 385=R`, in this order — RefMsgType(372) leads
each entry because it is the group delimiter in `FIX44.xml`. Parsing the Logon back round-trips
the list exactly (same count, same pairs, same order).

An empty list emits **no** 384 group at all (not even `384=0`) — byte-identical to today.

---

## 4. Receive an XMLnonFIX (35=n) payload intact (A-034)

No configuration is needed — this already works and this release pins it with a test.

An inbound `35=n` message carrying an XML document in the `XmlDataLen(212)`/`XmlData(213)`
length-delimited pair (including a payload with embedded SOH `0x01` and `'='` bytes) is:
- delivered to your `Application::fromApp` callback (as an opaque application message),
- **not** routed to `fromAdmin`,
- **not** rejected — including when opt-in dictionary validation (`validate_inbound_messages =
  true`) is enabled,

and reading tag `213` returns the exact original bytes, embedded SOH preserved.

```cpp
// Illustrative — match your Application::fromApp signature/return exactly
// (fromApp returns an expected/error; a returned error triggers a
// BusinessMessageReject, session.cpp:3504-3510 — it is NOT void).
struct MyApp : fixpp::session::Application {
    fixpp::core::expected_t<void> fromApp(
        fixpp::wire::MessageView<...>& mv, SessionId sid) override {
        if (mv.msg_type() == "n") {
            auto xml = mv.get(213);   // byte-exact XmlData, embedded SOH intact
            // ... hand the opaque XML to your application layer ...
        }
        return {};
    }
};
```

---

## Defaults recap (the "do nothing" contract)

| Config field | Default | Effect when default |
|--------------|---------|---------------------|
| `posture` | `std::nullopt` | No posture enforcement; no `464` emitted |
| `advertised_max_message_size` | `std::nullopt` | No `383` emitted; no negotiated size enforcement (absolute backstop unchanged) |
| `supported_msg_types` | `{}` | No `384` group emitted |
| (A-034) | — | 35=n already delivered to `fromApp` byte-exact |

With all three unset/empty, the engine is byte-for-byte and disposition-for-disposition
identical to the pre-070 baseline.
