# Data model: 093-inbound-frame-dispositions

Each entity names its visibility: public (an installed header), private (`src/`), or C ABI. Shapes are
design intent. The types the implementation lands are authoritative, and Gate A may revise these.

## E-1: `Framer::Config::resync_on_garble` and `garble_record`

Public and additive, in `include/fixpp/wire/framer.hpp`.

- `bool resync_on_garble = false;` is a new `Config` member. With it `false`, `feed` is unchanged byte
  for byte.
- `struct garble_record { core::error kind; std::size_t discarded; };` describes one disregarded garbled
  region.
  - `kind` is the Framer's error: `wire_framing_resync`, `wire_invalid_body_length` or
    `wire_checksum_mismatch`.
  - `discarded` is the bytes dropped up to the next frame start.
- `std::optional<garble_record> last_garble() const noexcept;` is reset by every `feed`.
- New private state: one bit, "the previous byte was SOH / we are inside a garble region", which
  persists across feeds (FR-002). It does not change `sizeof(Framer)` beyond that one flag.
- Invariant: with the flag on, a `feed` call reports at most one garble. That garble precedes every
  frame the call produces.
- Ordering change for every caller: `frame_len > max_frame_bytes` is tested as soon as `body_off` is
  known, before the checksum (FR-013).

## E-2: The inbound limit L and the parse buffer

Private, `Session`.

- `std::uint32_t inbound_limit_` is set in `open()`: the advertised 383 if set, else 65536. `open()`
  refuses a value under 4096 or over 262144.
- `std::span<std::byte> inbound_parse_buf_` holds B(L) bytes, allocated once in `open()` from
  `session_arena_` (research R-3) and released at destruction. It is `mutable`, because
  `validate_inbound_` is `const`.
- B(L) = 12·⌊L/3⌋ + 4·pow2ceil(1.25·⌊L/3⌋ + 1) + `kCallbackReadHeadroom` + `kContainerSlack`.
  - The two constants are named, and their values are a measurement recorded in `research.md`, not
    written into a comment.
  - `kContainerSlack` covers MSVC-debug container proxies. A per-lane cell measures the peak with
    `pmr_allocation_tracking_resource` and asserts peak ≤ B(L).
- `OffsetTable::Config::max_offset_entries = ⌊L/3⌋ + 1` for every inbound parse.
- `std::uint32_t inbound_limit() const noexcept` is read by the pump.

## E-3: The parse reserve argument

Public and additive, on `OffsetTable::build` and `Parser::parse` (or an overload).

- A per-call `std::size_t reserve_entries` hint. The table does not store it, and clones and reifies do
  not copy it.
- Default 0, which is today's behaviour, so every existing caller is unchanged.

## E-4: Garbled-frame accounting

Private, `Session`.

- `std::atomic<std::uint64_t> garbled_frames_`, monotonic. It is incremented with relaxed ordering on the
  strand and may be read from any thread.
- `void note_garbled_frame_(const wire::garble_record&) noexcept` increments the counter, emits
  `session_event_garbled_frame` and logs.
- `void replay_first_frame_garbles_(std::uint32_t count, wire::garble_record last) noexcept` is called by
  the engine after `open()` with the counts from the first-frame read.
- `std::uint64_t garbled_frame_count() const noexcept` is a public C++ accessor on `Session`.
- The logger is resolved once at `open()`: `cfg_.logger_override`, else the engine's logger. It may be
  null, which `FIXPP_SLOG` tolerates.

## E-5: New `SessionEvent` alternatives

Public and additive, in `include/fixpp/session/session_event.hpp`. Both are appended to the variant;
precedent is #424's `session_event_resend_slot_gap_filled`.

- `session_event_garbled_frame { core::error kind; std::uint32_t discarded_bytes; }`
- `session_event_establishment_timeout { }`
- `session_event_close_reset_wait_expired { }` (FR-041's bounded wait)
- The 16-slot ring can evict older events under a garble flood. That is disclosed; the counter (E-4) is
  the durable signal.

## E-6: Establishment deadline

Private, the engine pump.

- `run_read_pump(..., std::optional<steady_time_point> establish_deadline)`.
  - Acceptor: accept time + `logon_timeout`.
  - Initiator: the time `drive_reconnect` returns + `logon_timeout`.
- While `!session.has_reached_active()`, each read races the deadline through `await_deadline` on
  `engine_cfg.clock`. After the first Active, reads are plain.
- `bool Session::has_reached_active() const noexcept` reads a **dedicated** latch, `reached_active_`. It
  is set unconditionally in `record_state_transition_` on the first entry to Active, before its
  `engine_.application == nullptr` early return.
  - `onLogon_fired_` cannot serve: it latches only when an application is attached, so a session with
    none would never disarm the deadline and would be closed while Active.
  - Today each `Session` serves one connection: `run_connect_loop` and the accept loop each construct a
    Session per connection. If a Session is ever reused for a second connection, the latch must reset
    when the transport is installed. Re-derive this at implementation.

## E-7: `SessionConfig::logon_timeout_ms`

Public and additive.

- `std::uint32_t logon_timeout_ms{10000};` Zero is refused by `open()`, by TOML and by the C setter.
- The C ABI setter is `fixpp_session_config_set_logon_timeout_ms(fixpp_session_config_t*, uint32_t ms)`.
- The TOML key is `logon_timeout_ms` (or a duration string, if the loader's duration convention applies;
  decided at implementation against `kRecognized`).

## E-8: C-ABI garbled-frame getter

- `fixpp_error_t fixpp_session_garbled_frame_count(const fixpp_session_t*, uint64_t* out)`.
- A null handle or a null `out` is refused. Before the session exists it writes 0. It is thread-safe.
- Python exposes it through `%apply` OUTPUT, with a GIL-table row.

## E-9: `MessageStore::reset_to`

Public, additive and non-pure.

- `virtual asio::awaitable<core::expected_t<void>> reset_to(seqnum_t next_in, seqnum_t next_out) noexcept;`
- The default body runs `co_await reset()` and then advances each counter to its target with
  `next_seqnum(dir, true)`. The first error is returned.
- The overrides in `MemoryStore` and `FileStore` are atomic: no reader, and no restart after a crash,
  sees an intermediate state.
- The four pure virtuals stay four, against Article XIV §2's limit of five.

## E-10: Reset-unit-in-flight flag

Private, `Session`.

- `bool reset_unit_in_flight_` is set across the unit's single `reset_to` await and cleared after it,
  error paths included.
- `close()` waits on it before its teardown reset, polling the way it already waits for the liveness
  counter. The bound and expiry are plan.md OD-1. On expiry it proceeds and records E-5's
  `session_event_close_reset_wait_expired`.
- It is cleared on every exit path of the unit, including an `operation_aborted` from total cancellation
  (research R-9).

## State and disposition changes

`contracts/inbound-frame-dispositions.md` C-2 has the per-state table. It is not reproduced here.
