# Data model: 093-inbound-frame-dispositions

Each entity names its visibility: public (an installed header), private (`src/`), engine seam (private
state reached through `session_engine_access`, E-11), or C ABI. Shapes are design intent. The types
the implementation lands are authoritative, and Gate A may revise these.

## E-1: `Framer::Config::resync_on_garble` and `garble_summary`

Public and additive, in `include/fixpp/wire/framer.hpp`. Contract C-1 has the rules.

- `bool resync_on_garble = false;` is a new `Config` member. With it `false`, `feed` is unchanged byte
  for byte, including the order of the `frame_len > max_frame_bytes` check.
- `struct garble_summary { std::uint32_t regions; core::error first_kind; std::size_t discarded; };`
  describes what one `feed` call disregarded.
  - `regions` is the number of garbled regions the call opened. A region continued from an earlier
    call is not counted again.
  - `first_kind` is the first opened region's kind: `wire_framing_resync`, `wire_invalid_body_length`
    or `wire_checksum_mismatch`.
  - `discarded` is every byte the call dropped, including those of a continued region.
- `garble_summary last_garbles() const noexcept;` returns the summary, which every `feed` resets.
- New private state: `bool searching_`, true while a garbled region is open across feeds. The carry
  holds at most the four trailing bytes that are a proper prefix of `8=FIX`. There is no
  "previous byte was SOH" state, because the post-garble start rule is `8=FIX` anywhere (C-1).
- With the flag on, and only then:
  - `frame_len > max_frame_bytes` is tested as soon as `body_off` is known, before the CheckSum
    (FR-013, OD-4);
  - the BeginString and BodyLength scans are bounded (C-1 W-2);
  - the carry is compacted only when the incoming bytes would not fit after what it holds, so a
    carry-only feed never compacts (C-1 W-1).
- The session path adds a fourth kind of its own, `core::error::wire_header_out_of_order` (an existing
  code), for criterion 3 (C-2 step 1). The Framer never produces it.

## E-2: The inbound limit L, the carry and the parse buffer

Engine seam and private, `Session`.

- `std::uint32_t inbound_limit_for(SessionConfig const&) noexcept` is one free function, used by
  `register_session`'s gate, by the accept loop's first-frame read and by `Session::open()`. It returns
  the advertised 383 if set, else 65536. `register_session` and `open()` refuse a value under 4096 or
  over 262144 (OD-2). The accept loop runs only for a registered entry, so it never sees one.
- `std::uint32_t inbound_limit_` is set in `open()` from that function.
- The carry is allocated in `open()` from `cfg_.framer_carry_arena` (else `new_delete_resource()`), with
  capacity L plus the pump's read size. A `bad_alloc` is an `open()` error. The construction path is a
  throwing one: `pmr_carry_buffer`'s `noexcept` constructor would turn the failure into
  `std::terminate`. The pump borrows the carry through the seam.
- `std::span<std::byte> inbound_parse_buf_` holds B(L) bytes. It is allocated once in `open()` from
  `session_arena_` (research R-3) and released at destruction. It is `mutable`, because
  `validate_inbound_` is `const`.
- With N(L) = ⌊L/3⌋ + 1, the most fields a frame of L bytes can hold plus one:
  B(L) = 12·N(L) + 4·`overlay_cap_for`(N(L)) + `kAlignPad` + `kCallbackReadHeadroom` +
  `kContainerSlack`.
  - `kAlignPad` covers aligning the entry and overlay blocks inside one monotonic resource.
  - The three constants are named, and their values are a measurement recorded in `research.md`, not
    written into a comment.
  - `kContainerSlack` covers MSVC-debug container proxies. A per-lane cell measures the peak with
    `pmr_allocation_tracking_resource` and asserts peak ≤ B(L).
- `OffsetTable::Config::max_offset_entries = N(L)` for every inbound parse. Each parse reserves
  `min(N(L), frame.size()/3 + 1)` entries, which never exceeds what B(L) budgets.
- Per-session cost: the carry plus B(L). Research R-3 has the worked totals.

## E-3: The parse reserve argument

Public and additive, on `OffsetTable::build` and `Parser::parse` (or an overload).

- A per-call `std::size_t reserve_entries` hint. The table does not store it, and clones and reifies
  do not copy it.
- Default 0, which is today's behaviour, so every existing caller is unchanged.

## E-4: Garbled-frame accounting

Private `Session` state, with a public reader.

- `std::atomic<std::uint64_t> garbled_frames_` is monotonic. It is incremented with relaxed ordering on
  the strand and may be read from any thread.
- **Placement.** It lives on the `Session`. The engine builds one `Session` per `SessionEntry` per
  `Engine::start()`: both role loops `co_return` after one connection, and `start()` runs once.
  `lookup()` keeps returning that `Session` after its connection ends. So the counter is monotonic per
  SessionId for the engine's life, and the C getter reads it through the reader snapshot. If the engine
  ever serves a second connection per entry, this placement must be re-derived (plan.md, Gate A
  departures). To re-derive, read what follows the `run_read_pump` call in `run_accept_loop` and in
  `run_connect_loop` (`src/session/engine.cpp`), and the lifecycle note above `class Engine` in
  `include/fixpp/session/engine.hpp`.
- `void note_garbles_(wire::garble_summary const&) noexcept` (seam) adds `regions` to the counter. When
  `regions > 0` it emits one `session_event_garbled_frame` and logs, rate-bounded (E-12). The pump calls
  it after every feed whose summary is non-empty. The accept loop calls it once, after `open()`, with
  the first-frame read's summary. C-2 step 1 calls it with `{1, wire_header_out_of_order,
  frame.size()}`.
- `std::uint64_t garbled_frame_count() const noexcept` is a public C++ accessor on `Session`.

## E-5: New `SessionEvent` alternatives

Public and additive, in `include/fixpp/session/session_event.hpp`. All are appended to the variant;
the precedent is #424's `session_event_resend_slot_gap_filled`.

- `session_event_garbled_frame { core::error first_kind; std::uint32_t frames; std::uint32_t
  discarded_bytes; }`. There is one per non-empty summary. `first_kind` is one of E-1's four kinds.
  `discarded_bytes` saturates at `UINT32_MAX`.
- `session_event_establishment_timeout { }`
- `session_event_close_reset_wait_expired { }` (FR-041's bounded wait)
- The 16-slot ring can evict older events under a garble flood. That is disclosed (L-9), and the
  counter (E-4) is the durable signal.

## E-6: Establishment deadline

Private, the engine pump and the accept loop.

- `run_read_pump(..., std::optional<steady_time_point> establish_deadline)`.
  - Acceptor: accept time + `logon_timeout`. The same deadline bounds the first-frame read
    (C-4 phase a), whose return becomes `{offset, len, garble_summary}`. The engine then slices the
    first frame and the surplus at `offset`, not at 0.
  - Initiator: the time `drive_reconnect` returns + `logon_timeout`.
- While `!has_reached_active()`, each read races the deadline through `await_deadline` on
  `engine_cfg.clock`. After the first Active, reads are plain.
- `bool has_reached_active() const noexcept` (seam) reads a **dedicated** latch, `reached_active_`. It
  is set unconditionally in `record_state_transition_` on the first entry to Active, before its
  `engine_.application == nullptr` early return.
  - `onLogon_fired_` cannot serve: it latches only when an application is attached, so a session with
    none would never disarm the deadline and would be closed while Active.
  - E-4's placement condition applies here too: a `Session` reused for a second connection would need
    the latch reset when the transport is installed.

## E-7: `SessionConfig::logon_timeout_ms`

Public and additive.

- `std::uint32_t logon_timeout_ms{10000};` Zero is refused by `register_session`, by `open()`, by the
  TOML loader and by the C setter.
- The C ABI setter is `fixpp_session_config_set_logon_timeout_ms(fixpp_session_config_t*, uint32_t ms)`.
- The TOML key is `logon_timeout_ms`, a bare integer of milliseconds, as `logout_disconnect_timeout_ms`
  is. Its mapper requires an integer with 0 < v ≤ `UINT32_MAX`. It refuses a non-integer as a type
  mismatch, and refuses zero, a negative value or an out-of-range value as out of range.

## E-8: C-ABI garbled-frame getter

- `fixpp_error_t fixpp_session_garbled_frame_count(const fixpp_session_t*, uint64_t* out)`.
- A null handle or a null `out` is refused. Before the session exists it writes 0. It is thread-safe.
- Python exposes it through `%apply` OUTPUT, with a GIL-table row.

## E-9: `MessageStore::reset_to`

Public, additive and non-pure.

- `virtual asio::awaitable<core::expected_t<void>> reset_to(seqnum_t next_in, seqnum_t next_out) noexcept;`
- Precondition: `next_in, next_out ∈ {1, 2}`. Any other value returns `session_invalid_argument` with
  no effect. The only caller, the 141=Y unit, passes values in that set (research R-6).
- The default body runs `co_await reset()`, then `next_seqnum(dir, true)` once for each target that is
  2. The first error is returned.
- The overrides in `MemoryStore` and `FileStore` are one operation under the store's writer lock: no
  reader, and no restart after a crash, sees an intermediate state.
- The four pure virtuals stay four, against Article XIV §2's limit of five.

## E-10: Reset-unit-in-flight flag and completion signal

Private, `Session`.

- `bool reset_unit_in_flight_` is set across the unit's single `reset_to` await and cleared after it,
  error paths included.
- A completion signal, which the unit raises when it clears the flag. One shape is an
  `asio::steady_timer` armed at `time_point::max()` and cancelled by the unit. The implementation picks
  the shape, but it must not poll.
- `close()` awaits that signal raced against `await_deadline(*effective_clock_, now + logon_timeout_ms)`,
  and only when it is about to issue its teardown reset with the flag set (C-6). On expiry it records
  E-5's `session_event_close_reset_wait_expired` and proceeds.
- The unit runs under C-6's cancellation shield, so no cancellation interrupts the await. The flag's
  exit paths are the store's own results.

## E-11: `session_engine_access`

Engine seam. `friend struct session_engine_access;` is declared in `include/fixpp/session/session.hpp`
and defined in a header under `src/session/` that is not installed. It is B21's shape, but for the
engine rather than for tests, so it is not gated on `FIXPP_TEST_HOOKS`.

- It exposes to `run_read_pump` and the accept loop:
  - `inbound_limit()`;
  - `has_reached_active()`;
  - `note_garbles_()`;
  - `note_establishment_timeout_()`;
  - the borrowed carry.
- Nothing in the installed `Session` surface gains an underscore-suffixed engine hook.

## E-12: Garble log records

Private, `Session`; the session's first production log site (OD-5).

- `FIXPP_SLOG` with the session's `trace_context`, through the logger resolved once at `open()`:
  `cfg_.logger_override`, else the engine's. It may be null, which `FIXPP_SLOG` tolerates.
- Every format string is registered in `src/log/format_registry.cpp`.
- Rate bound: at most one record per heartbeat interval (the value the session's Logon advertises). A
  record carries the kind and bytes of the garble that triggered it, and the number of garbles counted
  since the previous record but not logged.

## State and disposition changes

`contracts/inbound-frame-dispositions.md` C-2 has the per-state table. It is not reproduced here.
