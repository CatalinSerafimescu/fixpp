# Data model: 092-garbled-frame-reject

Terms (spec.md "Terminology"): a **framed but unparseable** frame passes the Framer and fails the
full parse; **garbled** is reserved for the FIX-SL 2020 §4.5.2 framing criteria; **disregard** is the
named disposition "no Reject, no advance, no disconnect", applied by 092 only to scan-faulty frames
(contract C-2). A Framer failure stays session-fatal (`L-004-4`; its disregard is fixpp#514).

## E-0 — `field_fault` (public, additive, `include/fixpp/wire/tag_scan.hpp`)

```cpp
namespace fixpp::wire {
enum class field_fault : std::uint8_t { none = 0, malformed_tag = 1, length_data_mismatch = 2 };
}
```

One enum for both readers that report a fault: the session's header scan (E-1) and the Parser's
field iterator (E-4). `tag_scan.hpp` is already included by both (`parser.hpp` directly, the scan
through its own include), so neither reader gains an include, and the fault vocabulary exists once.

| Kind | Bytes that raise it |
|---|---|
| `malformed_tag` | a non-digit tag byte; a tag above 0xFFFF; an empty tag (`=` first); a field with no `=` before SOH or before the end of the buffer |
| `length_data_mismatch` | a Data value counted by the Length field immediately before it whose counted extent runs past the end of the buffer, reaches the end exactly, or is not followed by SOH |

These are exactly the encoding-failure writers of `OffsetTable::build`. Derivation:
`grep -n "status_ = " src/wire/offset_table.cpp`, restricted to `OffsetTable::build`, split by
status: `err_invalid_field_format()` and `err_tag_out_of_range()` are encoding failures (the tag
sites map to `malformed_tag`, the counted-value sites to `length_data_mismatch`);
`err_offset_table_full()` and the `bad_alloc` arm's `out_of_memory` are resource failures and are
not faults (research R-2, R-4).

## E-1 — `FrameHeader` fault record and header identification (private, `src/session/scan_frame_header.hpp`)

New members, appended to the existing struct:

| Member | Type | Meaning |
|---|---|---|
| `fault` | `fixpp::wire::field_fault` | the first field the scan could not read; `none` for a fault-free frame |
| `fault_length_tag` | `std::uint16_t` | for `length_data_mismatch`, the tag of the field immediately before the faulting Data field (the Length that armed the carry: `length_data_carry::read_value` arms only from the previous field); else 0 |
| `fault_offset` | `std::uint32_t` | byte offset of the faulting field's first byte. **Instrument only**: the differential corpus (research R-2) asserts it against the planted mutation. It is never emitted (the Reject Text is fixed, R-5) and never logged (there is no session logger, R-4) |
| `msg_type_is_third` | `bool` | the third field of the frame is well-formed and its tag is 35. False when the third field is itself the faulting field, or when the scan faults before reaching it |
| `fault_ref_seq_num` | `std::string_view` | the **first** well-formed 34 the scan meets, written once on first sight. Contract C-2 reads it only when `fault != none` |
| `fault_ref_msg_type` | `std::string_view` | the third field's value when `msg_type_is_third` is true; else empty. Contract C-2 reads it only when `fault != none` |

**Existing members keep today's semantics** (research R-1): `msg_seq_num` and `msg_type` stay
last-wins, so a fault-free frame is scanned byte for byte as today (contract C-3 I-3). The two
`fault_ref_*` members answer a different question, "which 34 and 35 address the Reject of a frame
that has no parse", and only the disposition reads them. They are also written on fault-free frames,
because the differential instrument compares them there (C-3 I-4).

**Fault kinds, and which scan branch sets each:**
- `malformed_tag`: the tag-digit loop sees a non-digit byte or `accumulate_tag_digit` refuses (tag
  above 0xFFFF); the byte after the tag is not `=` (SOH or end of frame reached first); or the tag
  is empty (`=` is the field's first byte). Today the first three `continue` past the field, and an
  empty tag scans as tag 0.
- `length_data_mismatch`: `length_data_carry::read_value` returns nullopt. Today this returns the
  header without a record.

**Rules:**
- On the first fault the scan returns immediately. Every member not yet assigned stays empty, so a
  field "read before the fault" is exactly a non-empty member.
- **"34 read"** (used by contract C-2) means `parse_seqnum(fault_ref_seq_num) > 0`: the first 34
  lies wholly before the fault and is a positive integer. A `34=abc` or `34=0` before the fault is
  not "read" (D-7).
- A frame with no fault leaves `fault == none`, and every pre-existing member is populated exactly
  as today.
- `sizeof(FrameHeader)` grows. Size pins can live in `tests/` too: before changing it, run
  `git grep -n "sizeof(FrameHeader)\|sizeof(fixpp::session::detail::FrameHeader)\|FrameHeader) ==" -- src tests bench`.

## E-2 — Disposition step (private, `Session`)

- Each `on_inbound_frame` state arm branches **inline and synchronously** on
  `hdr.fault != field_fault::none` right after its scan. Only on that branch does it `co_await` the
  private coroutine `Session::dispose_unparseable_(hdr, state)`, and then the arm returns. The
  clean path adds one compare and no coroutine frame. That follows the precedent `validate_inbound_`
  set when it was made synchronous so that "the PASS path … is coroutine-frame-free and alloc-free"
  (research R-3).
- The NotConnected arm has no unconditional scan ahead of its guards today: it scans inside the
  validation block, and otherwise only after `interpret_logon` succeeds. Its existing scan is
  hoisted to the top of the arm and reused by the validate gate and the post-`interpret_logon`
  block, so the arm scans once per frame, as today on the accept path (R-3).
- The mapping from (state, `hdr`) to action is contract C-2.

## E-3 — Dispatch result and late parse failure (private)

`parse_and_dispatch_` returns `core::expected_t<void>` today, and a parse failure returns success.
It gains a distinct way to say "the parse failed and the parse target's receive callback
(`fromAdmin`/`fromApp`) did not run": a private result
enum, or a private sentinel `core::error` that never leaves the session. `validate_inbound_` returns
`std::optional<RejectDecision>` today, and a parse failure returns `nullopt`, which its callers read
as "no reject". It gains a third outcome, "parse failed".

Every late inbound call site of either treats "parse failed" as **terminal**: it closes the
session (`close(close_mode::terminal)`), sends no Reject, does not invoke that receive callback
(`onLogout` from the close and callbacks fired earlier at the site are out of scope), and returns
(contract C-6). There is one
action, not a per-site table. The call sites that parse a frame fixpp built are unchanged. The site
population and its derivation are in contract C-6 and research R-4.

## E-4 — Field-iterator fault record (public, additive, `include/fixpp/wire/parser.hpp`)

`MessageView<Mode>::field_iterator` gains:

```cpp
[[nodiscard]] field_fault fault() const noexcept;             // sticky: the first fault any advance() observed
[[nodiscard]] std::uint16_t fault_length_tag() const noexcept; // the Length tag when fault() == length_data_mismatch; else 0
```

It also gains private members to hold them, and the tag of the Length that armed the carry
(`prev_data_tag_` holds the Data tag, not the Length tag). **What the iterator yields does not
change.** `src/capi/message_read.cpp`'s `scan_slice_for_tag` walks group slices with this iterator.
A slice may legitimately end where a whole frame could not, so changing what is yielded would add
C-ABI effects. Only the fault record is new.

**Every stop or tolerance in `advance()`.** Derive it by reading every `done_ = true` and every
early `return` in `MessageView<Mode>::field_iterator::advance`, then checking each against the
`OffsetTable::build` writers in E-0:

| # | Condition in `advance()` | Yield today (unchanged) | `fault()` after 092 |
|---|---|---|---|
| S0 | `pos_ >= buf_.size()` | end (`done_`) | unchanged (`none` if nothing earlier) |
| S1 | a non-digit tag byte | stop (`done_`) | `malformed_tag` |
| S2 | `accumulate_tag_digit` refuses (tag above 0xFFFF) | stop | `malformed_tag` |
| S3 | no `=` before SOH or the end | stop | `malformed_tag` |
| S4 | counted value: `end < size` and the byte at `end` is not SOH | stop | `length_data_mismatch` |
| T1 | an empty tag (`=` at `pos_`) | yields tag 0, continues | `malformed_tag` |
| T2 | counted value: the count exceeds the bytes left | yields the value clamped to the end | `length_data_mismatch` |
| T3 | counted value: `end == size` (nothing terminates it) | yields the value | `length_data_mismatch` |

T1–T3 are the three places the iterator is more tolerant than `OffsetTable::build`. They stay
tolerant in what they yield, and they now report.

## E-5 — `dictionary_driven_validator::validate` fault handling (public behaviour, FR-012)

- `validate` hoists its Step 1 iterator out of the `for` init, so the fault is readable after the
  loop.
- At the **top of each iteration**, before it examines the yielded field, and once more **after the
  loop**, it checks `it.fault()`:
  - `malformed_tag` → returns `core::error::wire_invalid_tag_number` and leaves `*ref_tag_out`
    untouched (the validator's contract: never invent a tag, so 371 is omitted);
  - `length_data_mismatch` → writes `it.fault_length_tag()` to `*ref_tag_out` and returns
    `core::error::wire_length_data_mismatch`.
- The check has to come before the field checks. Otherwise T1's yielded tag 0 would fail the
  unexpected-tag check first and surface as 373=2.
- Steps 2 onward do not run after a fault.

## E-6 — New `core::error` enumerators (public, additive, `include/fixpp/core/error.hpp`)

| Enumerator | Slot | `error_message` | `reject_reason_map` → 373 | C mapping (`src/capi/error.cpp` `translate`) |
|---|---|---|---|---|
| `wire_invalid_tag_number` | 132 | a new message | 0 | `FIXPP_ERR_WIRE_INVALID_FRAME` |
| `wire_length_data_mismatch` | 133 | a new message | 5 | `FIXPP_ERR_WIRE_INVALID_FRAME` |

- **Slots.** They are appended at the next contiguous slots after `app_payload_malformed = 131`, per
  `[const §X.4]` append-only, with explicit values.
- **C mapping.** Both map to the C code `wire_invalid_field_format` already maps to. No C code is
  added, so `introducing_minor` and `tools/abi_history/error_codes_v1.txt` are untouched. The
  mapping exists for the total switch only:
  `git grep -n "dictionary_driven_validator\|validator_->validate\|\.validate(" -- src/capi` finds no
  C-ABI caller of the validator. The mapping is **not** layer-independent: a tag above 0xFFFF is
  `wire_tag_out_of_range` from `OffsetTable::build` (→ `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`) and
  `wire_invalid_tag_number` from the validator (→ `FIXPP_ERR_WIRE_INVALID_FRAME`). One cell pins
  both mappings for an over-0xFFFF tag (quickstart §1).
- **Returned only by** `dictionary_driven_validator::validate` (E-5). `OffsetTable::build`'s codes
  are unchanged.
- **Every pin that enumerates or bounds the error set moves with them.** The population and its
  derivation are in research R-7.

## E-7 — `SeqnumManager` inbound bound (public class, behaviour only; FR-019)

No member, type or signature changes. `SeqnumManager::check_inbound`'s in-sequence branch gains one
state rule:

| `next_inbound_` before | `seq` | Result | `next_inbound_` after |
|---|---|---|---|
| < seqnum_max | = `next_inbound_` | ok (unchanged) | +1 (unchanged) |
| = seqnum_max | = seqnum_max | `store_seqnum_overflow` (new; today: ok) | seqnum_max (today: 0) |
| any | ≠ `next_inbound_` | too-low / too-high (unchanged) | unchanged |

- `store_seqnum_overflow` is the existing error `assign_outbound` returns at the outbound bound. No
  `core::error` value is added.
- `set_next_inbound`, `hydrate` and `reset_to_one` store a `seqnum_t` without arithmetic, so none
  can wrap. seqnum_max is a valid stored NextNumIn (research R-14).
- `Session::consume_rejected_seqnum_` keeps its signature. On `store_seqnum_overflow` it now takes
  the Disconnected transition and returns the error, where every `check_inbound` error used to mean
  "not consumed" (success). Its other outcomes are unchanged.
