# Phase 1 Data Model — 004-wire-codec

Source of truth: `[2b §4]` (public API), `[2b §6]` (behavioral contract), `[2b §8]` (PMR). Entities are wire-format/structural — they alias bytes, they do not model FIX business semantics.

## Entities

### E1 — `View` (flyweight base)

- **Fields:** `std::byte const* data_`, `std::size_t len_`, debug-only `detail::generation_token gen_` (`(pool_id:uint16, gen:uint16)`, `[[no_unique_address]]`, stripped in release).
- **Invariants:** non-owning; lifetime ⊆ originating frame buffer. Trivially copyable in release. Users cannot mint a `View` (only `Parser`/`Framer` produce them). `check_alive()` traps in debug if pool generation rotated past `gen_`.
- **Relationships:** base of E3/E4/E6/E8/E9; `entry` (E5) spans alias it.

### E2 — `Framer` + `pmr_carry_buffer`

- **Fields:** `Config{ max_frame_bytes = default_max_frame_bytes (256 KiB) }`; `pmr_carry_buffer` = fixed-capacity PMR vector sized `max_frame_bytes + small const` at construction, backed by `SessionConfig::framer_carry_arena` (**session lifetime**).
- **Invariants:** never reallocates; bounded growth → `wire_frame_too_large`. CheckSum verification mandatory (no production switch). Not thread-safe — one per session, on the I/O strand. Owns no memory beyond `Config`.
- **State:** `pending_bytes()` exposes partial-read carry size between `feed()` calls.

### E3 — `frame_view : View`

- **Represents:** one complete FIX message, BodyLength-validated + CheckSum-verified.
- **Accessors:** `body()` (between `9=…|`'s SOH and `10=`'s `1`), `bytes()` (full `8=…|…|10=NNN|`). v0.1 `checksum()`/`body_length()` removed (Codex P3).
- **Relationships:** input to `Parser::parse`.

### E4 — `MessageView<Mode> : View`

- **Common:** `msg_type()`, `msg_seq_num()`, `begin()/end()` (`field_iterator`).
- **Index-only (`requires Mode==Index`):** `offsets()`, `get<Tag>()`/`get(tag)`, `group<NoTag,GroupT>()`, `unknown_fields()`.
- **Invariants:** aliases the originating `frame_view` buffer; capturing past `fromApp` is UB in release / debug-trap. Cross-strand escape only via `MessageView::reify(mr)` (owned by 2c). All view-returning members `[[clang::lifetimebound]]` on `*this`; all `expected_t<>` returns `[[nodiscard]]`.

### E5 — `OffsetTable` + `entry`

- **`entry`:** `{ uint16_t tag; uint32_t offset; uint32_t length; uint16_t group_index_link; }` — **`static_assert(sizeof(entry)==12 && alignof(entry)==4)`** (`[2b §1.2]`/`[2b §4.4]`). One entry per field **occurrence**, not per distinct tag. `group_index_link==0` ⇒ top-level.
- **Structure:** `entry[]` array + open-address robin-hood overlay (cap = next-pow2 ≥ 1.25·n); group sub-index `pmr::vector<pmr::vector<entry>>`-shaped, **lazy** on first `group(no_tag)`.
- **Invariants:** all storage from the captured per-message `mr` (must outlive the table); `find` O(1) avg / O(n) worst; entries in document order.
- **Caps:** `default_max_offset_entries = 4096` (occurrence) → `wire_offset_table_full`; tag ∈ `uint16_t` → `wire_tag_out_of_range`.

### E6 — `Writer` + `group_writer`

- **Fields:** `dst` (caller buffer), `scratch_mr` (group bookkeeping arena). Tracks `body_start_offset`/`body_end_offset`.
- **Behavior:** `append_raw`/`append<T>` (traits `to_chars`), `open_group(no_tag,count)` (LIFO nesting), `commit() &&` → digit-only BodyLength (`memmove` backpatch, worst-case one frame-sized move) + byte-sum-mod-256 CheckSum; returns total bytes. `group_writer` RAII `close()` seals counts.
- **Invariants:** `noexcept`; zero alloc for group-free messages; bounded `memmove`.

### E7 — `Validator` (interface) + `dictionary_driven_validator`

- **Pure-virtual (exactly 5, `[const §XIV.2]`):** `validate(msg, scratch_mr)`, `validate_field(tag, value)`, `required_fields(msg_type)`, `field_valid_for(msg_type, tag)`, `group_first_field(no_tag)`.
- **Default impl:** holds `dict::table_view` by value (value type — no virtual wire→dict edge); per-version (v42/v44/v50sp2/vt11). `validate` is **unconditional** over every dictionary-known field present (type/enum/length/conditional-required), not per-accessor.
- **Scratch:** `seen[]` bitmap (~512 B at cap) + `required_remaining` sorted `pmr::vector<uint16_t>` (~10–50 B); ≤ ~600 B worst-case from `scratch_mr`. `validate_field` allocation-free.

### E8 — `group_view<GroupT> : View`

- `size()`, `operator[](i)` (uses lazy sub-index), `iter()` (streaming opt-out — walks raw bytes via dictionary first-field-of-group rule, no sub-index). `iter()` and `operator[]` MUST enumerate identical entries/order (seam #8).

### E9 — `unknown_fields_view : View`

- Filtered view over the offset table yielding `(tag,value)` for **dictionary-missing** tags only. Dictionary-known-but-invalid-for-MsgType tags are NOT here — they produce `wire_unexpected_tag` (validator rule 5). No vector materialization; round-trip writes them back in original byte order via in-place two-pointer merge (zero alloc).

### E10 — `field_iterator`

- Forward iterator over fields; in Iter mode uses a static `constexpr` table of FIX-standard Length+Data pairs to skip SOH inside `data` fields (Iter mode is dict-free). Three pointers + debug token; zero alloc.

## State / lifetime model

| Arena | Lifetime | Holds | Reset by |
|---|---|---|---|
| `SessionConfig::message_arena` | per-message (reset after `fromApp`) | OffsetTable `entry[]` + hash overlay + lazy group sub-indices + validator scratch + writer group bookkeeping | session FSM after `fromApp` |
| `SessionConfig::framer_carry_arena` | session | `pmr_carry_buffer` (sized once) | session destruction |
| `Writer::scratch_mr` (ctor param) | one outbound build turn | `group_writer` bookkeeping | caller after `commit()` |

Buffer-pool generations rotate on per-message arena reset; the session FSM ensures rotation happens **after** `fromApp` returns. Capturing any `View`-derived past that point: UB (release) / trap (debug).

## Error mapping (`fixpp::core::error`, additive — `[const §X.4]` non-renumbering)

Current `error.hpp` max occupied slot = **29** (`dict_reify_wire_body_not_ready`). The 13 `[2b §6.7]` wire variants append at **slots 30–42** (exact base re-confirmed against `core/error.hpp` at `/implement`; append below slot 29, never renumber):

| Variant (proposed slot) | Source `[2b]` | Remediation |
|---|---|---|
| `wire_frame_too_large` (30) | §6.1.3 | hostile/misconfig — disconnect |
| `wire_invalid_body_length` (31) | §6.1.3 | malformed — disconnect |
| `wire_checksum_mismatch` (32) | §6.1.5 | corruption/hostile — disconnect |
| `wire_framing_resync` (33) | §6.1.2 | garbage between frames — disconnect/resync |
| `wire_invalid_field_format` (34) | §6.2 | malformed mid-field — Session-Reject + disconnect |
| `wire_offset_table_full` (35) | §1.2/§4.4 | cap exceeded — Session-Reject; raise cap if trusted venue |
| `wire_group_too_large` (36) | §1.2/§4.4 | per-group cap — Session-Reject; raise cap if trusted |
| `wire_tag_out_of_range` (37) | §1.2 | tag > uint16 — Session-Reject |
| `wire_required_field_missing` (38) | §6.5.4 | conformance — Session-Reject |
| `wire_header_out_of_order` (39) | §6.5.1 | conformance — Session-Reject |
| `wire_field_value_out_of_range` (40) | §6.5.3 | conformance — Session-Reject |
| `wire_field_value_truncated` (41) | from 2a §6.4, surfaced unchanged | precision loss — Session-Reject + log |
| `wire_unexpected_tag` (42) | §6.5.5 | dict-known tag invalid for MsgType — Session-Reject `SessionRejectReason=2` (`[FIX50SP2 §2.1]`) |

- v0.1's implicit `wire_tag_count_exceeded` is **deleted** (the dropped distinct-tag cap, Root cause #1).
- C-ABI coalescing target for 2i (recorded, not implemented here, D-13): framing/protocol → `FIXPP_ERR_WIRE_INVALID_FRAME`; capacity → `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`; conformance → `FIXPP_ERR_WIRE_CONFORMANCE`; precision → reuse 2a `FIXPP_ERR_DECIMAL_PRECISION_LOSS`.
- Cutover note: `dict_reify_wire_body_not_ready (29)` (003's stub-not-ready signal) becomes unreachable once the real `MessageView` lands; it is **kept** (non-renumbering) and comment-annotated as cutover-obsolete, not deleted.

## PMR / allocation accounting (`[const §VIII.5]`, `[2b §6.6]`)

- **Iter path:** 0 allocations end-to-end.
- **Index parse:** OffsetTable `entry[]` + hash overlay from per-message arena; ≤ §1.2 caps (worst-case ≈ 80 KiB at the 4096-entry cap).
- **Validator.validate:** ≤ ~600 B from `scratch_mr`. `validate_field`: 0.
- **Writer:** group bookkeeping from `scratch_mr`; 0 for group-free messages. One bounded `memmove` at `commit()`.
- **Global heap between parse and `fromApp`: 0** — verified by `tools/check_alloc.py` under `mallocnesia` (seam #10) and the three-arena pinning test (seam #13).
